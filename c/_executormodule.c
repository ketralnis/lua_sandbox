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

#define abs_index(L, i) \
    ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : \
    lua_gettop(L) + (i) + 1)

int install_control_block(lua_State *L, size_t max_memory,
                          PyObject* references) {
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

    // our python refcounting strategy is to add python objects here. when we
    // have objects in here, we're signalling that python can't clean them up.
    // doing this instead of explicit refcounting lets us deal better with
    // reference cycles. See executor.py:Lua.__init__ for more details
    control->references = references;

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
}


void finish_runtime_limiter(lua_State *L) {
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);

    if(!(control->runtime).enabled) {
        fprintf(stderr, "runtime limiter was not enabled\n");
    }

    lua_sethook(L, NULL, 0, 0);

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

    PyObject* ret = PyObject_CallFunction(call_proxy, "OOi",
                                          executor,
                                          capsule->val,
                                          capsule->raw_lua_args);

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

    // make a capsule so we can safely get it back to Python land
    store_python_capsule(L, pvalue, 0, 0, 0);

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
                          int should_cache,
                          int recursive,
                          int raw_lua_args) {
    lua_capsule* capsule =
        (lua_capsule*)lua_newuserdata(L, sizeof(lua_capsule));

    capsule->val = val;
    capsule->cache_ref = LUA_REFNIL; // cache is populated lazily
    capsule->cache = should_cache;
    capsule->recursive = recursive;
    capsule->raw_lua_args = raw_lua_args;

    // assign the metatable of the userdata to get the methods
    lua_getfield(L, LUA_REGISTRYINDEX, EXECUTOR_LUA_CAPSULE_KEY);
    lua_setmetatable(L, -2);

    // we don't do explicit refcounting. instead we rely on our references dict
    // to keep us live and free_python_capsule cleans it up. See
    // executor.py:Lua.__init__ for details
    lua_control_block *control = NULL;
    (void*)lua_getallocf(L, (void*)&control);
    PyObject* references = control->references;

    // from here on out we must escape through error below
    // this is all equivalent to references.setdefault(id(val), []).append(val)

    // this is how cpython derives the builtin `id` function
    // https://github.com/python/cpython/blob/29500737d45cbca9604d9ce845fb2acc3f531401/Python/bltinmodule.c#L1207
    PyObject* cycle_key = PyLong_FromVoidPtr(val);
    PyObject* list = NULL;

    if(cycle_key == NULL) {
        goto error;
    }

    list = PyDict_GetItem(references, cycle_key);
    // list is now a borrowed reference or NULL
    if(list == NULL) {
        list = PyList_New(1); // now we own list
        if(list == NULL) {
            // couldn't allocate
            goto error;
        }
        if(PyDict_SetItem(references, cycle_key, list) == -1) {
            goto error;
        }
    } else {
        Py_INCREF(list); // so we own it and can free it later
    }
    // now there's a list we can append to
    if(PyList_Append(list, val) == -1) {
        goto error;
    }

error:
    Py_XDECREF(cycle_key);
    Py_XDECREF(list);
    // we never owned val
}


int free_python_capsule(lua_State *L) {
    lua_capsule* capsule =
        (lua_capsule*)luaL_checkudata(L, 1, EXECUTOR_LUA_CAPSULE_KEY);
    // can longjmp out
    luaL_argcheck(L, capsule != NULL, 1, "python capsule expected");

    // the upvalue for this function, installed in
    // executor.py:Lua.install_python_capsule (which sets up the capsule
    // metatable). Note that even though references is available on the control
    // block, we may be called after the control block has been freed so we get
    // our own separate copy
    PyObject* references = lua_touserdata(L, lua_upvalueindex(1));
    luaL_argcheck(L, references != NULL, -1, "upvalue missing?");

    // clean up the cache
    if(capsule->cache_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, capsule->cache_ref);
    }

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    // from here on out we must escape through error below

    PyObject *list=NULL, *key=NULL, *popped=NULL;

    // this is how cpython derives the builtin `id` function
    key = PyLong_FromVoidPtr(capsule->val);
    if(key == NULL) {
        PyErr_WarnEx(NULL, "free_python_capsule couldn't make key", 0);
        PyErr_Print(); // we can't really raise exceptions here
        goto error;
    }

    list = PyDict_GetItem(references, key);

    // we can't really raise exceptions here. if one of these is happening
    // we're probably leaking memory
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
        int del_ret = PyDict_DelItem(references, key);
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
    luaL_argcheck(L, index_proxy != NULL, -1, "upvalue missing?"); // can longjmp out

    PyObject* executor = lua_touserdata(L, lua_upvalueindex(2));
    luaL_argcheck(L, executor != NULL, -1, "upvalue missing?"); // can longjmp out

    // upvalue[1] points to the Python proxy function for extracting the key,
    // args[-2] points to the capsule struct, and args[-1] is the index to the
    // key they are trying to look up
    int key_idx = lua_gettop(L);

    // stack is [key]

    disable_limit_memory(L);
    // with the memory limiter disabled, we must now exit through finish_no_gil

    if(capsule->cache
       && check_capsule_cache(L, capsule, key_idx)) {
        // we've already computed this before, and check_capsule_cache put it
        // on the stack and ready to return
        // stack is now [key, value]
        goto finish_no_gil;
    }

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    // stack is [key]

    lua_pushvalue(L, key_idx); // he'll consume this and leave the return value for us
    PyObject* ret = PyObject_CallFunction(index_proxy, "OOii",
                                          executor,
                                          capsule->val,
                                          capsule->cache,
                                          capsule->recursive);
    // he either raises an exception or leaves the result at the top of the Lua
    // stack
    if(ret == NULL) {
        // fixes the memory limiter and the GIL too
        return translate_python_exception(L, gstate);
    }
    Py_DECREF(ret);
    PyGILState_Release(gstate);

    // stack is [key, value]

    // now we have the result at the top of the stack, we can put it in the
    // cache for next time
    if(capsule->cache) {
        int value_idx = lua_gettop(L);
        set_capsule_cache(L, capsule, key_idx, value_idx);
    }

finish_no_gil:
    enable_limit_memory(L);

    // stack is [key, value]. swap and pop the key, leaving only the value
    lua_insert(L, -2);
    lua_pop(L, 1);

    // stack is [value]

    return 1;
}


static int check_capsule_cache(lua_State* L, lua_capsule* capsule, int key_idx) {
    /*
     * Check the ref cache for this capsule to see if the given key has already
     * been cached. If so, put it at the top of the stack and return true,
     * otherwise don't mess with the stack and return false
     */

    if(capsule->cache_ref == LUA_REFNIL) {
        // if this capsule doesn't have a cache, nothing can be in it
        return 0;
    }

    key_idx = abs_index(L, key_idx);

    create_capsule_cache(L, capsule);
    // stack is [cache]
    lua_pushvalue(L, key_idx);
    // stack is [cache, key]
    lua_rawget(L, -2); // pops the key
    // stack is [cache, result]
    if(lua_isnil(L, -1)) {
        // it wasn't there, revert the stack to before we started
        lua_pop(L, 2); // pops the nil and the cache
        return 0;
    }


    // stack is [cache, container] because the result is a 1-item array container
    lua_rawgeti(L, -1, 1);
    // stack is [cache, container, actual_result]. shift stuff around so we can return it
    lua_insert(L, -3);
    // stack is [actual_result, cache, container
    lua_pop(L, 2);
    // stack is [actual_result]. success!
    return 1;
}


static void set_capsule_cache(lua_State* L, lua_capsule* capsule,
                              int key_idx, int value_idx) {
    key_idx = abs_index(L, key_idx);
    value_idx = abs_index(L, value_idx);

    create_capsule_cache(L, capsule);
    // stack is [cache]
    lua_pushvalue(L, key_idx);
    // stack is [cache, key]
    lua_createtable(L, 1, 0); // the 1-item container with room for us
    // stack is [cache, key, container]
    lua_pushvalue(L, value_idx);
    // stack is [cache, key, container, value]
    lua_rawseti(L, -2, 1); // adds the value to the container, pops the value
    // stack is [cache, key, container]
    // now we're all set to insert the container into the cache
    lua_rawset(L, -3); // pops the key and container
    // stack is [cache];
    lua_pop(L, 1);// clean up after ourselves
}


static void create_capsule_cache(lua_State* L, lua_capsule* capsule) {
    /*
     * Bring the ref cache for this capsule to the top of the stack, creating
     * it if necessary
     */
    if(capsule->cache_ref == LUA_REFNIL) {
        // this capsule hasn't had a cache created yet, so create it
        lua_createtable(L, 0, 1); // the cache itself with room for our new value
        // stack is [cache]
        lua_pushvalue(L, -1); // so we have two copies of the table for when luaL_ref pops it off the stack
        // stack is [cache, cache]
        capsule->cache_ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops it off the stack, leaving our duplicate
        // stack is [cache] and the cache is now in the registry
    } else {
        // push it on the stack
        lua_rawgeti(L, LUA_REGISTRYINDEX, capsule->cache_ref);
    }
}


PyObject* lua_string_to_python_buffer(lua_State* L, int idx) {
    // our caller already checked that it's a string.  we insist that it's
    // actually a string because (1) otherwise wanting a buffer into it doesn't
    // make sense and (2) lua_tolstring will *convert* it into a string,
    // destructively
    size_t size = 0;
    void* ptr = lua_tolstring(L, idx, &size);
    PyObject* buff = PyBuffer_FromMemory(ptr, size); // new reference
    // return that reference. it's up to our caller to free it, and to *not*
    // keep a reference to it after the string is no longer on the stack
    return buff;
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
