#include <stdio.h>
#include <time.h>

#include <Python.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#if LUA_VERSION_NUM == 501
#include "luajit.h"
#endif

#include "_executormodule.h"

#define EXECUTOR_XSTR(s) EXECUTOR_STR(s)
#define EXECUTOR_STR(s) #s


l_runtime_limiter* new_runtime_limiter(lua_State *L, double max_runtime) {
    clock_t now = clock();

    l_runtime_limiter* limiter =
        (l_runtime_limiter*)lua_newuserdata(L, sizeof(l_runtime_limiter));

    limiter->start = now;
    limiter->max_runtime = max_runtime;
    // calculate the expires now so we don't have to do floating point
    // arithmetic on every invocation
    limiter->expires = (clock_t)(now+(double)max_runtime*(double)CLOCKS_PER_SEC);

    lua_setfield(L, LUA_REGISTRYINDEX, EXECUTOR_RUNTIME_LIMITER_KEY);

    return limiter;
}


void time_limiting_hook(lua_State *L, lua_Debug *_ar) {
    // find the runtime limiter

    lua_pushstring(L, EXECUTOR_RUNTIME_LIMITER_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);

    if(lua_isnil(L, -1)) {
        // limiting isn't turned on right now
        lua_pop(L, 1); // remove the nil
        return;
    }

    // otherwise the userdata is on the stack now

    l_runtime_limiter* limiter = (l_runtime_limiter*)lua_touserdata(L, -1);
    lua_pop(L, 1); // get the userdata back off the stack

    clock_t now = clock();

    if(now > limiter->expires) {
        // they have gone on too long

        // calculate the duration so we can add it to the error message
        clock_t dur_cl = now-limiter->start;
        double dur_s = (double)dur_cl/(double)CLOCKS_PER_SEC;

        luaL_error(L, "runtime quota exceeded %f>%f",
                   dur_s, limiter->max_runtime);
        // unreachable
    }
}


void free_runtime_limiter(lua_State *L, l_runtime_limiter* runtime_limiter) {
    // delete it from lua
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, EXECUTOR_RUNTIME_LIMITER_KEY);
}


l_alloc_limiter* new_memory_limiter(lua_State* L, size_t max_memory) {

#if LUA_VERSION_NUM == 501
    void* old_ud = NULL;
    lua_Alloc old_allocf = lua_getallocf(L, &old_ud);
#endif

    l_alloc_limiter* limiter = malloc(sizeof(l_alloc_limiter));

    if(limiter == NULL) {
        return NULL;
    }

#if LUA_VERSION_NUM == 501
    limiter->old_ud = old_ud;
    limiter->old_allocf = old_allocf;
#endif

    limiter->memory_limit = max_memory;
    limiter->memory_used = 0;
    limiter->limit_allocation = 0;

    return limiter;
}


void free_memory_limiter(lua_State *L, l_alloc_limiter* limiter) {

#if LUA_VERSION_NUM == 501

    if(limiter!=NULL) {
        lua_setallocf(L, limiter->old_allocf,
                      limiter->old_ud);
    }

#endif

    free(limiter);
}


void* l_alloc_restricted (l_alloc_limiter* limiter,
                          void *ptr, size_t o_old_size, size_t new_size) {
    size_t old_size = o_old_size;

    if(ptr == NULL) {
        /*
         * <http://www.lua.org/manual/5.2/manual.html#lua_Alloc>:
         * When ptr is NULL, old_size encodes the kind of object that Lua is
         * allocating.
         *
         * Since we don't care about that, just mark it as 0
         */
        old_size = 0;
    }

    int enable_limiter = limiter->limit_allocation && limiter->memory_limit;

    size_t new_total = limiter->memory_used;
    new_total -= old_size;
    new_total += new_size;

    if (enable_limiter && new_total>limiter->memory_limit) {
        /* too much memory in use */
        return NULL;
    }

#if LUA_VERSION_NUM == 501
    ptr = limiter->old_allocf(limiter->old_ud, ptr, o_old_size, new_size);

#else
    ptr = realloc(ptr, new_size);
#endif

    if (ptr) {
        /* reallocation successful */
        limiter->memory_used = new_total;
    }

    return ptr;
}

void enable_limit_memory(l_alloc_limiter *limiter) {
    if(limiter != NULL)
        limiter->limit_allocation = 1;
}


void disable_limit_memory(l_alloc_limiter *limiter) {
    if(limiter != NULL)
        limiter->limit_allocation = 0;
}


int call_python_function_from_lua(lua_State *L) {
    // we're only willing to call functions of 0 arguments so that we don't have
    // to do argument handling here in C. LuaValue._from_python will wrap the
    // real Python function in one that does argument handling and give that one
    // to us

    // make sure it's actually one of our userdatas before doing anything else
    python_callable *callable =
        (python_callable*)luaL_checkudata(L, 1, EXECUTOR_LUA_CALLABLE_KEY);
    luaL_argcheck(L, callable != NULL, 1, "pyfunction expected"); // can longjmp out

    // find the allocation limiter so we can disable it
    lua_pushstring(L, EXECUTOR_MEMORY_LIMITER_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);

    // there's a chance that this is nil
    l_alloc_limiter *limiter =
        (l_alloc_limiter*)lua_touserdata(L, -1);
    lua_pop(L, 1); // get the userdata back off the stack

    // once we hold the GIL it's vital that we turn off the allocation checking
    // because any allocation failure will longjmp out and we'll have no chance
    // to release it
    if(limiter!=NULL) limiter->limit_allocation = 0;

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject* ret = PyObject_CallFunctionObjArgs(callable->callable,
                                                 callable->executor,
                                                 callable->val,
                                                 NULL);

    if(ret == NULL) {
        // there will be a Python exception on the stack. Translate it into a
        // Lua exception and clear it

        PyObject *ptype=NULL, *pvalue=NULL, *ptraceback=NULL;
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);
        PyErr_Clear();

        char* error_message = "unknown error executing Python code";

        PyObject* repr = PyObject_Repr(pvalue);
        if(repr == NULL) {
            // fine we just won't use it then
            PyErr_WarnEx(NULL, "call_python_function_from_lua couldn't make repr", 0);
            PyErr_Print();
            PyErr_Clear();
        } else if(!PyString_CheckExact(repr)) {
            // if repr doesn't return a string we can't use it
            PyErr_WarnEx(NULL, "got non string from PyObject_Repr", 0);
        } else {
            error_message = PyString_AsString(repr);
        }

        lua_pushstring(L, error_message);

        Py_XDECREF(repr);
        Py_XDECREF(ptype);
        Py_XDECREF(pvalue);
        Py_XDECREF(ptraceback);

        // release the gil
        PyGILState_Release(gstate);
        if(limiter!=NULL) limiter->limit_allocation = 1;

        // raise the error on the lua side (longjmps out)
        lua_error(L);

        return 0; // unreachable

    } else {
        // otherwise we were successful and the return value is now at the top
        // of the stack
        Py_DECREF(ret);

        PyGILState_Release(gstate);
        if(limiter != NULL) limiter->limit_allocation = 1;

        // we have no idea how long that call may have taken, so check this
        // hook just in case
        time_limiting_hook(L, NULL); // may not return

        return 1; // one return value that the wrapper left on the stack
    }
}


#if LUA_VERSION_NUM == 501

static int memory_panicer (lua_State *L) {
    // if we're out of memory, this might not even work
    lua_getfield(L, LUA_REGISTRYINDEX, EXECUTOR_JMP_RETURN_KEY);
    jmp_buf *jb = lua_touserdata(L, -1);
    lua_pop(L, 1);

    longjmp(*jb, 1);

    return 0; // unreachable
}

int memory_safe_pcallk(lua_State *L, int nargs, int nresults, int _msgh) {
    jmp_buf jb;

    int i_excepted = setjmp(jb);
    int ret;

    lua_CFunction oldppanicer = lua_atpanic(L, memory_panicer);

    if(i_excepted == 0) {
        // try

        // there's only one of these globals which keeps us from being safely
        // recursive
        lua_pushlightuserdata(L, &jb);
        lua_setfield(L, LUA_REGISTRYINDEX, EXECUTOR_JMP_RETURN_KEY);

        ret = lua_pcall(L, nargs, nresults, 0);

        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, EXECUTOR_JMP_RETURN_KEY);

    } else {
        // except (the memory_panicer ran)

        ret = LUA_ERRMEM;
    }

    // restore the old one
    lua_atpanic(L, oldppanicer);

    return ret;
}

#endif


void store_python_callable(lua_State *L,
                           PyObject* executor,
                           PyObject* callable,
                           PyObject* val,
                           long cycle_key,
                           PyDictObject* cycles) {
    // our caller already added us to cycles so we don't have to worry about
    // it here
    python_callable* freeable =
        (python_callable*)lua_newuserdata(L, sizeof(python_callable));

    // we don't hold the GIL so we can't do anything but store pointers

    freeable->executor = executor;
    freeable->callable = callable;
    freeable->val = val;
    freeable->cycle_key = cycle_key;
    // note that we don't have to worry about refcounting this dict because it
    // is guaranteed to outlive the lua_State
    freeable->cycles = cycles;
}


int free_python_callable(lua_State *L) {
    python_callable* freeable =
        (python_callable*)luaL_checkudata(L, 1, EXECUTOR_LUA_CALLABLE_KEY);
    // can longjmp out
    luaL_argcheck(L, freeable != NULL, 1, "pyfunction expected");

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    // from here on out we must escape through error below

    PyObject *list=NULL, *key=NULL, *popped=NULL;

    key = PyLong_FromLong(freeable->cycle_key);
    if(key == NULL) {
        PyErr_WarnEx(NULL, "free_python_callable couldn't make key", 0);
        PyErr_Print(); // we can't really raise exceptions here
        goto error;
    }

    list = PyDict_GetItem((PyObject*)freeable->cycles, key);
    if(list == NULL || !PyList_Check(list) || PyList_GET_SIZE(list)==0) {
        PyErr_WarnEx(NULL, "free_python_callable dangling reference", 0);
        PyErr_Print(); // we can't really raise exceptions here
        goto error;
    }

    // it doesn't really matter which reference we pop
    popped = PyObject_CallMethod(list, "pop", NULL);
    if(popped == NULL) {
        PyErr_WarnEx(NULL, "free_python_callable couldn't pop", 0);
        PyErr_Print(); // we can't really raise exceptions here
        goto error;
    }

    if(PyList_GET_SIZE(list)==0) {
        // we emptied it out, so remove the entry entirely
        int del_ret = PyDict_DelItem((PyObject*)freeable->cycles, key);
        if(del_ret==-1) {
            PyErr_WarnEx(NULL, "free_python_callable couldn't delitem", 0);
            PyErr_Print(); // we can't really raise exceptions here
            goto error;
        }
    }

error:

    // 'list' is either NULL or a borrowed reference (and may now have been
    // freed)
    Py_XDECREF(key);
    Py_XDECREF(popped);

    PyGILState_Release(gstate);
    return 0; // number of return values
}


PyObject* decapsule(python_callable* callable) {
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject* ret = callable->val;
    Py_INCREF(ret);

    PyGILState_Release(gstate);

    return ret;
}


static int add_int_constant(PyObject* module, char* name, int value) {
    PyObject *as_int = PyInt_FromLong(value);
    if(as_int == NULL) {
        return -1;
    }

    if(PyModule_AddObject(module, name, as_int) == 1) {
        return -1;
    }

    return 0;
}


static int add_str_constant(PyObject* module, char* name, char* value) {
    PyObject *as_str = PyString_FromString(value);
    if(as_str == NULL) {
        return -1;
    }

    if(PyModule_AddObject(module, name, as_str) == 1) {
        return -1;
    }

    return 0;
}


PyMODINIT_FUNC init_executor(void) {
    // initialise the module

    PyObject* module = NULL;

    module = Py_InitModule3("lua_sandbox._executor",
        NULL, /* no functions of our own */
        "C portion that implements the Lua-Python bridge");
    if (module == NULL) {
        /* exception raised in preparing */
        return;
    }

    /*
        Lua keeps a bunch of really important constants in #defines so they
        aren't accessible at runtime from ctypes. This just attempts to copy
        them into the module's namespace so they are accessible at runtime
    */

    if(add_int_constant(module, "LUA_REGISTRYINDEX", LUA_REGISTRYINDEX)==-1)
        goto error;

#ifdef LUA_GLOBALSINDEX
    if(add_int_constant(module, "LUA_GLOBALSINDEX", LUA_GLOBALSINDEX)==-1)
        goto error;
#endif

    if(add_int_constant(module, "LUA_TNIL", LUA_TNIL)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TBOOLEAN", LUA_TBOOLEAN)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TLIGHTUSERDATA", LUA_TLIGHTUSERDATA)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TNUMBER", LUA_TNUMBER)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TSTRING", LUA_TSTRING)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TTABLE", LUA_TTABLE)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TFUNCTION", LUA_TFUNCTION)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TUSERDATA", LUA_TUSERDATA)==-1)
        goto error;
    if(add_int_constant(module, "LUA_TTHREAD", LUA_TTHREAD)==-1)
        goto error;

    if(add_int_constant(module, "LUA_MULTRET", LUA_MULTRET)==-1)
        goto error;

    if(add_int_constant(module, "LUA_OK", LUA_OK)==-1)
        goto error;
    if(add_int_constant(module, "LUA_ERRSYNTAX", LUA_ERRSYNTAX)==-1)
        goto error;
    if(add_int_constant(module, "LUA_ERRRUN", LUA_ERRRUN)==-1)
        goto error;
    if(add_int_constant(module, "LUA_ERRMEM", LUA_ERRMEM)==-1)
        goto error;
    if(add_int_constant(module, "LUA_ERRERR", LUA_ERRERR)==-1)
        goto error;

    if(add_int_constant(module, "LUA_MASKCOUNT", LUA_MASKCOUNT)==-1)
        goto error;
    if(add_int_constant(module, "LUA_MASKRET", LUA_MASKRET)==-1)
        goto error;
    if(add_int_constant(module, "LUA_MASKLINE", LUA_MASKLINE)==-1)
        goto error;

    if(add_int_constant(module, "LUA_GCCOLLECT", LUA_GCCOLLECT)==-1)
        goto error;

    if(add_str_constant(module, "LUA_LIB_NAME", LUA_LIB_NAME)==-1)
        goto error;

    if(add_int_constant(module, "LUA_VERSION_NUM", LUA_VERSION_NUM)==-1)
        goto error;

#if LUA_VERSION_NUM == 501

    if(add_int_constant(module, "LUAJIT_MODE_ENGINE", LUAJIT_MODE_ENGINE)==-1)
        goto error;
    if(add_int_constant(module, "LUAJIT_MODE_FUNC", LUAJIT_MODE_FUNC)==-1)
        goto error;
    if(add_int_constant(module, "LUAJIT_MODE_ALLFUNC", LUAJIT_MODE_ALLFUNC)==-1)
        goto error;
    if(add_int_constant(module, "LUAJIT_MODE_ALLSUBFUNC", LUAJIT_MODE_ALLSUBFUNC)==-1)
        goto error;
    if(add_int_constant(module, "LUAJIT_MODE_OFF", LUAJIT_MODE_OFF)==-1)
        goto error;
    if(add_int_constant(module, "LUAJIT_MODE_ON", LUAJIT_MODE_ON)==-1)
        goto error;
    if(add_int_constant(module, "LUAJIT_MODE_FLUSH", LUAJIT_MODE_FLUSH)==-1)
        goto error;

#endif

    if(add_str_constant(module, "EXECUTOR_LUA_NUMBER_TYPE_NAME",
                        EXECUTOR_XSTR(LUA_NUMBER))==-1)
        goto error;

    return;

error:
    Py_XDECREF(module);

    return;
}
