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


int install_control_block(lua_State *L, size_t max_memory) {
    lua_control_block* control = malloc(sizeof(lua_control_block));

    if(control==NULL) {
        return 0;
    }

    void* old_ud = NULL;
    lua_Alloc old_allocf = lua_getallocf(L, &old_ud);

    (control->memory).enabled = 0;
    (control->memory).memory_used = 0;
    (control->memory).memory_limit = max_memory;
    (control->memory).old_allocf = old_allocf;
    (control->memory).old_ud = old_ud;
    (control->runtime).enabled = 0;

#if LUA_VERSION_NUM == 501
    control->panic_return = NULL;
#endif

    // it normally wouldn't be safe to change the allocator while the VM is
    // running, but this is safe because we're just proxying on to the same one
    // anyway
    lua_setallocf(L, (lua_Alloc)l_alloc_restricted, (void*)control);

    // now we abuse this control block to always be available from
    // lua_getallocf

    return 1;
}


void wrapped_lua_close(lua_State *L) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    // put the old allocator back. We have to put it back because Lua allocates
    // in order to creates the *ud so if we don't put it back we leak that data.
    // Note! Because we put it back, it's not safe to require access to the
    // control block in areas that may run during cleanup. This includes in
    // particular free_python_capsule.
    lua_setallocf(L, (control->memory).old_allocf, (control->memory).old_ud);

    lua_close(L);

    free(control);
}


void start_runtime_limiter(lua_State *L, double max_runtime, int hz) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    clock_t now = clock();

    if((control->runtime).enabled) {
        fprintf(stderr, "runtime limiter was already enabled\n");
    }

    (control->runtime).enabled = 1;

    (control->runtime).start = now;
    (control->runtime).max_runtime = max_runtime;
    // calculate the expires now so we don't have to do floating point
    // arithmetic on every invocation
    (control->runtime).expires =
        (clock_t)(now+(double)max_runtime*(double)CLOCKS_PER_SEC);

    lua_sethook(L, time_limiting_hook, LUA_MASKCOUNT, hz);

#if LUA_VERSION_NUM == 501
    // in order for that to apply to compiled code we have to turn off the
    // compiler :( there's still some win because luajit's interpreter is
    // still faster than canonical Lua's
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE|LUAJIT_MODE_OFF);
#endif
}


void finish_runtime_limiter(lua_State *L) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    if(!(control->runtime).enabled) {
        fprintf(stderr, "runtime limiter was not enabled\n");
    }

    lua_sethook(L, NULL, 0, 0);

#if LUA_VERSION_NUM == 501
    // can turn this back on now. note that there's no luaJIT_getmode so we
    // can't know if it was turned on before
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
#endif

    (control->runtime).enabled = 0;
}


static void time_limiting_hook(lua_State *L, lua_Debug *_ar) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    if(!(control->runtime).enabled) {
        fprintf(stderr, "time_limiting_hook called with no limiter\n");
        return;
    }

    clock_t now = clock();

    if(now>(control->runtime).expires) {
        // they have gone on too long

        // calculate the duration so we can add it to the error message
        clock_t dur_cl = now-(control->runtime).start;
        double dur_s = (double)dur_cl/(double)CLOCKS_PER_SEC;

        luaL_error(L, "runtime quota exceeded %f>%f",
                   dur_s, (control->runtime).max_runtime);
        // unreachable
    }
}


void* l_alloc_restricted(lua_control_block* control,
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

    size_t new_total = (control->memory).memory_used;
    new_total -= old_size;
    new_total += new_size;

    int kick_in = (control->memory).enabled
        && (control->memory).memory_limit
        // only if we're trying to grow (lua panics if we return NULL when
        // shrinking)
        && new_total>(control->memory).memory_used
        // we're using more than the limit
        && new_total>(control->memory).memory_limit;

    if (kick_in) {
        /* too much memory in use */
        return NULL;
    }

    ptr = (control->memory).old_allocf((control->memory).old_ud,
                                       ptr, o_old_size, new_size);

    if (ptr || new_size==0) {
        /* reallocation successful (free is always successful) */
        (control->memory).memory_used = new_total;
    }

    return ptr;
}


size_t get_memory_used(lua_State *L) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    return (control->memory).memory_used;
}


void enable_limit_memory(lua_State *L) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    (control->memory).enabled = 1;
}


void disable_limit_memory(lua_State *L) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    (control->memory).enabled = 0;
}


#if LUA_VERSION_NUM == 501

static int memory_panicer(lua_State *L) {
    // if we're out of memory, this might not even work
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    jmp_buf *jb = control->panic_return;

    longjmp(*jb, 1);

    return 0; // unreachable
}

int memory_safe_pcallk(lua_State *L, int nargs, int nresults, int _msgh) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    jmp_buf jb;

    jmp_buf* old_panic_return = control->panic_return;
    control->panic_return = &jb;

    lua_CFunction old_panicer = lua_atpanic(L, memory_panicer);

    int i_excepted = setjmp(jb);
    int ret;

    if(i_excepted == 0) {
        // try
        ret = lua_pcall(L, nargs, nresults, 0);

    } else {
        // except (the memory_panicer ran)
        ret = LUA_ERRMEM;
    }

    // restore the old ones
    lua_atpanic(L, old_panicer);
    control->panic_return = old_panic_return;

    return ret;
}

#endif


int call_python_function_from_lua(lua_State *L) {
    // we're only willing to call functions of 0 arguments so that we don't have
    // to do argument handling here in C. LuaValue._from_python will wrap the
    // real Python function in one that does argument handling and give that one
    // to us

    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    // make sure it's actually one of our userdatas before doing anything else
    lua_capsule *capsule =
        (lua_capsule*)luaL_checkudata(L, 1, EXECUTOR_LUA_CAPSULE_KEY);
    luaL_argcheck(L, capsule != NULL, 1, "python capsule expected"); // can longjmp out

    PyObject* call_proxy = lua_touserdata(L, lua_upvalueindex(1));
    luaL_argcheck(L, call_proxy != NULL, -1, "upvalue missing?");

    PyObject* executor = lua_touserdata(L, lua_upvalueindex(2));
    luaL_argcheck(L, executor != NULL, -1, "upvalue missing?");

    // once we hold the GIL it's vital that we turn off the allocation checking
    // because any allocation failure will longjmp out and we'll have no chance
    // to release it
    disable_limit_memory(L);

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject* ret = PyObject_CallFunction(call_proxy, "OO",
                                          executor,
                                          capsule->val);

    if(ret == NULL) {
        // fixes the memory limiter and the GIL too
        return translate_python_exception(L, gstate);
    }

    // otherwise we were successful and the return value is now at the top
    // of the stack

    Py_DECREF(ret);
    PyGILState_Release(gstate);
    enable_limit_memory(L);

    // we have no idea how long that call may have taken, so check this
    // hook just in case
    if((control->runtime).enabled) {
        time_limiting_hook(L, NULL); // may not return
    }

    return 1; // one return value that the wrapper left on the stack
}


static int translate_python_exception(lua_State *L, PyGILState_STATE gstate) {
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
    enable_limit_memory(L);

    // raise the error on the lua side (longjmps out)
    lua_error(L);

    return 0; // unreachable
}


void store_python_capsule(lua_State *L,
                          PyObject* val,
                          long cycle_key) {
    // our caller already added us to cycles so we don't have to worry about
    // it here
    lua_capsule* capsule =
        (lua_capsule*)lua_newuserdata(L, sizeof(lua_capsule));

    // store the index cache
    lua_newtable(L); // the cache itself
    int cache_ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops it off the stack

    // we don't hold the GIL so we can't do anything but store pointers. note
    // that we explicitly are not refcounting here: we rely on `cycles` to keep
    // us live
    capsule->val = val;
    capsule->cycle_key = cycle_key;
    capsule->cache_ref = cache_ref;
}


int free_python_capsule(lua_State *L) {
    lua_capsule* capsule =
        (lua_capsule*)luaL_checkudata(L, 1, EXECUTOR_LUA_CAPSULE_KEY);
    // can longjmp out
    luaL_argcheck(L, capsule != NULL, 1, "python capsule expected");

    PyObject* cycles = lua_touserdata(L, lua_upvalueindex(1));
    luaL_argcheck(L, cycles != NULL, -1, "upvalue missing?");

    // clean up the cache
    luaL_unref(L, LUA_REGISTRYINDEX, capsule->cache_ref);

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    // from here on out we must escape through error below

    PyObject *list=NULL, *key=NULL, *popped=NULL;

    key = PyLong_FromLong(capsule->cycle_key);
    if(key == NULL) {
        PyErr_WarnEx(NULL, "free_python_capsule couldn't make key", 0);
        PyErr_Print(); // we can't really raise exceptions here
        goto error;
    }

    list = PyDict_GetItem(cycles, key);

    // we can't really raise exceptions here
    if(list == NULL) {
        PyErr_WarnEx(NULL, "free_python_capsule dangling reference (not found)", 0);
        goto error;
    } else if(!PyList_Check(list)) {
        PyErr_WarnEx(NULL, "free_python_capsule dangling reference (not a list)", 0);
        goto error;
    } else if(PyList_GET_SIZE(list)==0) {
        PyErr_WarnEx(NULL, "free_python_capsule dangling reference (empty list)", 0);
        goto error;
    }

    // it doesn't really matter which reference we pop
    popped = PyObject_CallMethod(list, "pop", NULL);
    if(popped == NULL) {
        PyErr_WarnEx(NULL, "free_python_capsule couldn't pop", 0);
        PyErr_Print(); // we can't really raise exceptions here
        goto error;
    }

    if(PyList_GET_SIZE(list)==0) {
        // we emptied it out, so remove the entry entirely
        int del_ret = PyDict_DelItem(cycles, key);
        if(del_ret==-1) {
            PyErr_WarnEx(NULL, "free_python_capsule couldn't delitem", 0);
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


PyObject* decapsule(lua_capsule* capsule) {
    PyObject* ret = capsule->val;
    Py_INCREF(ret); // the caller gets a new reference
    return ret;
}


int lazy_capsule_index(lua_State *L) {
    lua_capsule *capsule =
        (lua_capsule*)luaL_checkudata(L, 1, EXECUTOR_LUA_CAPSULE_KEY);
    luaL_argcheck(L, capsule != NULL, 1, "python capsule expected"); // can longjmp out

    PyObject* index_proxy = lua_touserdata(L, lua_upvalueindex(1));
    luaL_argcheck(L, index_proxy != NULL, -1, "upvalue missing?");

    PyObject* executor = lua_touserdata(L, lua_upvalueindex(2));
    luaL_argcheck(L, executor != NULL, -1, "upvalue missing?");

    // upvalue[1] points to the Python proxy function for extracting the key,
    // args[-2] points to the capsule struct, and args[-1] is the index to the
    // key they are trying to look up
    int key_idx = lua_gettop(L);

    disable_limit_memory(L);

    /// see if it's already in the cache
    // put the cache on the stack
    lua_rawgeti(L, LUA_REGISTRYINDEX, capsule->cache_ref);
    // put the key on the stack
    lua_pushvalue(L, key_idx);
    // look at cache[key]
    lua_rawget(L, -2);
    // see if it's in there
    if(!lua_isnil(L, -1)) {
        // it was there! we can just leave it on the stack and return it
        // stack is [cache_table, cache_result];
        lua_insert(L, -2); // swaps the table and the result
        lua_pop(L, 1); // pops the table, leaving the result
        // success!
        goto finish_no_gil;

    } else {
        lua_pop(L, 2); // pop the nil and the cache table
    }

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject* ret = PyObject_CallFunction(index_proxy, "OOi",
                                          executor,
                                          capsule->val,
                                          -1);

    // he either raises an exception or leaves the result at the top of the Lua
    // stack
    if(ret == NULL) {
        // fixes the memory limiter and the GIL too
        return translate_python_exception(L, gstate);
    }

    Py_DECREF(ret);

    PyGILState_Release(gstate);

    if(!lua_isnil(L, -1)) {
        /// now we have the proper result at the top of the stack, we can put it in
        /// the cache for next time
        int value_idx = lua_gettop(L);
        // put the cache on the stack
        lua_rawgeti(L, LUA_REGISTRYINDEX, capsule->cache_ref);
        // put the key on the stack
        lua_pushvalue(L, key_idx);
        // put the value on the stack
        lua_pushvalue(L, value_idx);
        // stack now is [value, table, key, value] so -3 is the table
        lua_rawset(L, -3);
        // that pops the key and the value so stack is [value, table]
        lua_pop(L, 1); // leave only the value returned from Python
    }

finish_no_gil:
    enable_limit_memory(L);

    return 1;
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
