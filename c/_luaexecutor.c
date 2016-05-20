#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <pthread.h>
#include <sys/time.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <Python.h>

#include "_luaexecutor.h"

extern PyObject *LuaException;
extern PyObject *LuaOutOfMemoryException;

#if PY_VERSION_HEX < 0x02070000
// because we use PyCapsule
#error "I need Python >= 2.7.0"
#endif

#if LUA_VERSION_NUM == 501
// lua5.1 doesn't define this, but uses it the same way
#define LUA_OK 0
#elif LUA_VERSION_NUM == 502
#elif LUA_VERSION_NUM == 503
// note that we don't use integer types like Lua 5.3 does
#else
#error "I only know Lua 5.1 through 5.3"
#endif

#define abs_index(L, i) ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : \
                                       lua_gettop(L) + (i) + 1)

void* l_alloc_restricted (void *ud, void *ptr, size_t osize, size_t nsize) {
    _LuaExecutor* self = (_LuaExecutor*)ud;

    if(ptr == NULL) {
        /*
         * <http://www.lua.org/manual/5.2/manual.html#lua_Alloc>:
         * When ptr is NULL, osize encodes the kind of object that Lua is
         * allocating.
         *
         * Since we don't care about that, just mark it as 0
         */
        osize = 0;
    }

    if (nsize == 0) {
        free(ptr);
        self->memory_used -= osize; /* subtract old size from used memory */
        return NULL;
    }

    if (self->limit_allocation
        && self->memory_used + (nsize - osize) > self->memory_limit) {
        /* too much memory in use */
        return NULL;
    }

    ptr = realloc(ptr, nsize);
    if (ptr) {
        /* reallocation successful */
        self->memory_used += (nsize - osize);
    }

    return ptr;
}


void time_limiting_hook(lua_State *L, lua_Debug *ar) {
    // find our pointer back to self
    lua_pushlightuserdata(L, (void *)&EXECUTOR_LUA_REGISTRY_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);

    _LuaExecutor* self = (_LuaExecutor*)lua_touserdata(L, -1);
    lua_pop(L, 1); /* remove it from the stack now that we have it */

    struct timeval end;

    if(gettimeofday(&end, NULL)) {
        lua_pushstring(L, "error checking time quota");
        lua_error(L);
        return;
    }

    time_t diff = ((end.tv_usec+1000000*end.tv_sec)
                   - (self->script_started.tv_usec+1000000*self->script_started.tv_sec));

    if(self->max_lua_runtime != 0 && diff >= self->max_lua_runtime) {
        lua_pushstring(L, "time quota exceeded");
        lua_error(L);
        return;
    }
}

int encode_python_to_lua(lua_State* L, PyObject* value,
                         int recursion, int max_recursion) {
    /*
     * On error, sets a Python exception and returns 0
     */

    if(recursion > max_recursion) {
        PyErr_SetString(LuaException, "encode_python_to_lua recursed too far");
        return 0;

    } else if(!lua_checkstack(L, 1)) {
        PyErr_SetString(LuaException, "not enough lua stack space");
        return 0;

    } else if(value == Py_None) {
        lua_pushnil(L);

    } else if(PyBool_Check(value)) {
        if(value == Py_False) {
            lua_pushboolean(L, 0);
        } else {
            lua_pushboolean(L, 1);
        }

    } else if(PyInt_Check(value)) {
        long as_long = PyInt_AsLong(value);

        if(PyErr_Occurred()) {
            return 0;
        }

        lua_pushnumber(L, (double)as_long);

    } else if(PyLong_Check(value)) {
        long as_long = PyLong_AsLong(value);

        if(PyErr_Occurred()) {
            return 0;
        }

        lua_pushnumber(L, (double)as_long);

    } else if(PyFloat_Check(value)) {
        double as_double = PyFloat_AsDouble(value);

        if(PyErr_Occurred()) {
            return 0;
        }

        lua_pushnumber(L, as_double);

    } else if(PyString_Check(value)) {
        char* body = NULL;
        Py_ssize_t len = 0;

        if(PyString_AsStringAndSize(value, &body, &len)==-1) {
            return 0;
        }

#if LUA_VERSION_NUM == 501
        // unfortunately this will trigger Lua's exception handling
        lua_pushlstring(L, body, len);
#elif LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
        if(lua_pushlstring(L, body, len)==NULL) {
            PyErr_NoMemory();
            return 0;
        }
#endif

    } else if(PyTuple_CheckExact(value) || PyList_CheckExact(value)) {
        Py_ssize_t len = PySequence_Length(value);

        if(len == -1) {
            return 0;
        }

        lua_newtable(L);

        for(int i=0; i<len; i++) {
            PyObject *lvalue = PySequence_GetItem(value, i);

            if(lvalue == NULL) {
                lua_pop(L, 1); // the table
                return 0;
            }

            // the key
            lua_pushnumber(L, i+1);

            // the value
            if(!encode_python_to_lua(L, lvalue, recursion+1, max_recursion)) {
                Py_DECREF(lvalue);
                lua_pop(L, 2); // the key and table
                return 0;
            }

            Py_DECREF(lvalue);

            /* lua will pop off the key and value now, leaving only the table */
            lua_settable(L, -3);
        }

    } else if(PyDict_Check(value)) {
        lua_newtable(L);

        PyObject *dkey, *dvalue;
        Py_ssize_t pos = 0;

        while (PyDict_Next(value, &pos, &dkey, &dvalue)) {
            /*
             * serialize the key and value and leave them on the lua stack. if
             * either of these fail, they'll leave their error on the python
             * stack and return false
             */

            /*
             * To put values into the table, we first push the index, then the
             * value, and then call lua_rawset() with the index of the table in the
             * stack. Let's see why it's -3: In Lua, the value -1 always refers to
             * the top of the stack. When you create the table with lua_newtable(),
             * the table gets pushed into the top of the stack. When you push the
             * index and then the cell value, the stack looks like:
             *
             * <- [stack bottom] -- table, index, value [top]
             *
             * So the -1 will refer to the cell value, thus -3 is used to refer to
             * the table itself. Note that lua_rawset() pops the two last elements
             * of the stack, so that after it has been called, the table is at the
             * top of the stack.
             */

            if(!encode_python_to_lua(L, dkey, recursion+1, max_recursion)) {
                lua_pop(L, 1); // remove the table
                return 0;
            }
            if(!encode_python_to_lua(L, dvalue, recursion+1, max_recursion)) {
                lua_pop(L, 2); // remove the table and the key
                return 0;
            }

            /* lua will pop off the key and value now, leaving only the table */
            lua_settable(L, -3);
        }

    } else if(PyCallable_Check(value)) {
        // we implement functions as userdatas with contents of the pointer to
        // the PyObject* that have a metatable with __call and __gc, set up in
        // _LuaExecutor_init

        // put the userdata on the stack
        PyObject** value_userdata = (PyObject**)lua_newuserdata(L, sizeof(value));

        if(value_userdata==NULL) {
            // n.b. this won't happen because lua_newuserdata is one of those
            // lua functions that fails incorrectly, and we should only be in
            // here with allocation limits disabled anyway

            PyErr_Format(LuaOutOfMemoryException,
                         "reached lua memory limit");

            return 0;
        }
        *value_userdata = value;

        // assign its metatable to the one we set up on init
        luaL_getmetatable(L, EXECUTOR_LUA_FUNCTION_MT_KEY);
        lua_setmetatable(L, -2);

        // retain it (free_python_function will free it from __gc)
        Py_INCREF(value);

        // and leave it on the Lua stack

    } else {
        format_python_exception(PyExc_TypeError,
                                "cannot serialize unknown python type of %r",
                                value, NULL);
        return 0;
    }

    return 1;
}

static int call_python_function_from_lua(lua_State *L) {

    int ret = 0;
    PyObject* py_args = NULL;
    PyObject* result = NULL;

    // make sure we're being called correctly
    void *ud = luaL_checkudata(L, 1, EXECUTOR_LUA_FUNCTION_MT_KEY);
    luaL_argcheck(L, ud != NULL, 1, "pyfunction expected"); // longjmps out on failure

    // find our pointer back to self
    lua_pushlightuserdata(L, (void *)&EXECUTOR_LUA_REGISTRY_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);

    _LuaExecutor* self = (_LuaExecutor*)lua_touserdata(L, -1);
    lua_pop(L, 1); /* remove it from the stack now that we have it */

    /* from here on out, we must exit through the error/done labels */
    self->limit_allocation = FALSE;

    // get the gil. the self->l_mutex should already be held
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    // arg 1 is the userdata that contains the PyObject* address to the
    // function. 2..nargs are the arguments they have passed
    int nargs = lua_gettop(L);
    PyObject* py_callable = *(PyObject**)ud;

    py_args = serialize_lua_to_python_multi(L, 2, nargs-1, self->max_recursion);
    if(py_args == NULL) {
        goto error;
    }

    // now that that's in Python format, we don't need the lua version anymore
    lua_pop(L, 1+nargs); // the userdata and the nargs

    result = PyObject_CallObject(py_callable, py_args);
    if(result == NULL) {
        goto error;
    }

    ret = encode_python_to_lua(L, result, 0, self->max_recursion);

error:
    self->limit_allocation = TRUE;

    Py_XDECREF(py_args);
    Py_XDECREF(result);

    if(PyErr_Occurred()) {
        // there will be a Python exception on the stack. Translate it into a
        // Lua exception and clear it

        PyObject *ptype, *pvalue, *ptraceback;
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

        char* error_message = NULL;

        PyObject* repr = PyObject_Repr(pvalue);
        if(repr == NULL) {
            // fine we just won't use it then
            PyErr_Clear();
        } else if(!PyString_CheckExact(repr)) {
            // if repr doesn't return a string we can't use it
        } else {
            error_message = PyString_AsString(repr);
        }

        self->limit_allocation = FALSE;
        lua_pushstring(L, error_message);
        self->limit_allocation = TRUE;

        PyErr_Clear();
        Py_XDECREF(repr);

        // release the gil
        PyGILState_Release(gstate);

        lua_error(L);

        return 0; // unreachable

    } else {
        // release the gil
        PyGILState_Release(gstate);
        return ret;
    }
}

static int free_python_function(lua_State *L) {
    PyObject** fn = (PyObject**)lua_touserdata(L, -1);
    Py_DECREF(*fn);
    return 0; // number of return values
}

static void format_python_exception(PyObject* exc_type, const char *fmt, ...) {
    PyObject* err_format = NULL;
    PyObject* arg_tuple = NULL;
    PyObject* formatted = NULL;
    PyObject* p = NULL;
    int num_objs = 0;
    va_list argp;

    va_start(argp, fmt);

    err_format = PyString_FromString(fmt);
    if(err_format==NULL) {
        goto cleanup;
    }

    arg_tuple = PyTuple_New(0);
    if(arg_tuple==NULL) {
        goto cleanup;
    }

    while((p = va_arg(argp, PyObject*)) != NULL) {
        num_objs += 1;
        if(_PyTuple_Resize(&arg_tuple, num_objs) == -1) {
            goto cleanup;
        }
        Py_INCREF(p); // PyTuple_SET_ITEM will steal a reference but we borrowed it
        PyTuple_SET_ITEM(arg_tuple, num_objs-1, p);
    }

    formatted = PyString_Format(err_format, arg_tuple);
    if(formatted==NULL) {
        goto cleanup;
    }

    /* everything was successful, set the new object on the stack */
    PyErr_SetObject(exc_type, formatted);

cleanup:

    /* however we got here, an exception should be set on the stack */

    va_end(argp);

    Py_XDECREF(err_format);
    Py_XDECREF(arg_tuple);
    /*if formatted isn't null, then it's a good exception already on the stack*/
}


int serialize_python_to_lua(lua_State* L, PyObject* env, int max_recursion) {
    /*
     * take a lua_State and a Python Dict and encode that dict into global
     * variables in Lua. If this fails, it may leave the Lua stack in a bad way,
     * but we'll consider that okay because we'll dispose of it anyway
     */

    PyObject *key, *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(env, &pos, &key, &value)) {
        if(!PyString_Check(key)) {
            format_python_exception(PyExc_TypeError,
                                    "key %r is not str",
                                    key, NULL);
            return 0;
        }

        char* keystring = NULL;

        /* length is null to disallow \0 bytes */
        if(PyString_AsStringAndSize(key, &keystring, NULL)==-1) {
            return 0;
        }

        if(!encode_python_to_lua(L, value, 0, max_recursion)) {
            /* will have an exception on the python stack */
            return 0;
        }

        /* now the value is on the stack, save it to the global variable */
        lua_setglobal(L, keystring);
    }

    return 1;
}

PyObject* serialize_lua_to_python_multi(lua_State* L,
                                        int start_idx, int count,
                                        int max_recursion) {
    PyObject* pyresult = NULL;
    pyresult = PyTuple_New(count);

    if(pyresult==NULL) {
        goto error;
    }

    if(count==0) {
        // success, return the empty tuple
        goto done;
    }

    for(int i=0; i<count; i++) {
        int stacknum = abs_index(L, start_idx+i);

        PyObject* thisresult = serialize_lua_to_python(L, stacknum,
                                                       0, max_recursion);
        if(thisresult==NULL) {
            goto error;
        }
        // steals the thisresult ref, even if it fails
        if(PyTuple_SetItem(pyresult, i, thisresult)==-1) {
            goto error;
        }
    }

done:
    return pyresult;

error:
    Py_XDECREF(pyresult);
    return NULL;
}


PyObject* serialize_lua_to_python(lua_State* L, int idx,
                                  int recursion, int max_recursion) {
    /*
     * like serialize_python_to_lua, we can leave the Lua stack in a bad way,
     * but we declare this to be okay because we'll destroy it anyway. however,
     * we will not leave the Python state in any bad state (other than
     * potentially an exception set), as we are returning to Python afterwards
     */
    PyObject* ret = NULL;

    double as_double;
    int as_boolean;

    const char* string;
    size_t string_len;

    /* convert to absolute index because we'll mess with the stack */
    int e_idx = abs_index(L, idx);

    if(recursion > max_recursion) {
        PyErr_SetString(LuaException, "serialize_lua_to_python recursed too far");
        return NULL;
    }

    switch(lua_type(L, e_idx)) {

    case LUA_TNIL:
        ret = Py_None;
        Py_INCREF(ret);
        break;

    case LUA_TNUMBER:
        as_double = lua_tonumber(L, e_idx);
        ret = PyFloat_FromDouble(as_double);
        if(ret == NULL) {
            return NULL;
        }
        break;

    case LUA_TBOOLEAN:
        as_boolean = lua_toboolean(L, e_idx);
        if(as_boolean) {
            ret = Py_True;
        } else {
            ret = Py_False;
        }
        Py_INCREF(ret);
        break;

    case LUA_TSTRING:
        string = lua_tolstring(L, e_idx, &string_len);
        ret = PyString_FromStringAndSize(string, string_len);
        if(ret==NULL) {
            return NULL;
        }
        break;

    case LUA_TTABLE:
        /* the complex case */
        ret = PyDict_New();

        if(ret == NULL) {
            return NULL;
        }

        int table_index = abs_index(L, e_idx);

        lua_pushnil(L);  /* first key */

        while (lua_next(L, table_index) != 0) {
            /* `key' is at index -2 and `value' at index -1 */
            PyObject* key = serialize_lua_to_python(L, -2,
                                                    recursion+1, max_recursion);
            if(key == NULL) {
                lua_pop(L, 2); // key, value

                Py_DECREF(ret);
                return NULL;
            }

            PyObject* value = serialize_lua_to_python(L, -1,
                                                      recursion+1, max_recursion);
            if(value == NULL) {
                lua_pop(L, 2); // key, value

                Py_DECREF(key);
                Py_DECREF(ret);
                return NULL;
            }

            if(PyDict_SetItem(ret, key, value) == -1) {
                lua_pop(L, 2); // key, value

                Py_DECREF(key);
                Py_DECREF(value);
                Py_DECREF(ret);
                return NULL;
            }

            /* now the dictionary owns them */
            Py_DECREF(key);
            Py_DECREF(value);

            lua_pop(L, 1);  /* removes `value'; keeps `key' for next iteration */
        }

        // now just the table is on the stack, just like we got it

        break;

    default:
        PyErr_Format(LuaException,
                     "cannot serialize unknown Lua type %s",
                     lua_typename(L, lua_type(L, e_idx)));
        return NULL;
    }

    return ret;
}

static int _LuaExecutor_init(_LuaExecutor *self, PyObject *args, PyObject *kwargs) {
    int ret = 0;

    /*
     * the libraries require about 100k to run on their own. The default gives
     * them that plus some breathing room
     */
    Py_ssize_t max_lua_allocation = 2*1024*1024;
    /*
     * how many instruction cycles to execute before checking runtime
     */
    unsigned long long max_lua_cycles_hz = 500000;
    unsigned long long max_lua_runtime = 1000*1000; // 1 second
    /*
     * max depth of serialisation/deserialisation
     */
    unsigned long max_lua_depth = 10;

    static char *kwdlist[] = {
        "max_memory", "max_runtime", "max_cycles_hz",
        "max_object_depth",
        NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
                                     "|nKKk",
                                     kwdlist,
                                     &max_lua_allocation,
                                     &max_lua_runtime,
                                     &max_lua_cycles_hz,
                                     &max_lua_depth)) {
        return -1;
    }

    int have_lock = 0;

    self->memory_used = 0;
    self->memory_limit = max_lua_allocation;
    self->max_lua_runtime = max_lua_runtime;
    self->max_recursion = max_lua_depth;
    self->limit_allocation = FALSE;

    lua_State *L = NULL;

    if(pthread_mutex_init(&self->l_mutex, NULL) != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        goto error;
    }
    have_lock = 1;

    /*
     * All Lua contexts are held in this structure. We work with it almost
     * all the time.
     */

#if LUA_VERSION_NUM == 501
    L = luaL_newstate();
    /* set up our custom allocator */
    if(L!= NULL) lua_setallocf(L, l_alloc_restricted, (void*)self);
#elif LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
    L = lua_newstate(l_alloc_restricted, (void*)self);
#endif

    if(L == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    /*
     * TODO have to set a panic function in 501 since we can't catch pushlstring
     * OOMs, which are probably the most common type of them
     */

    /*
     * Load Lua libraries. We go ahead and load them all up here, but in
     * sandbox.lua we limit what can actually be called by the user
     */
    luaL_openlibs(L);

    /* install our time-limiting hook */
    lua_sethook(L, time_limiting_hook, LUA_MASKCOUNT, max_lua_cycles_hz);

    /*
     * add a pointer back to the _LuaExecutor object within the lua_State.
     * following the advice of <http://www.lua.org/pil/27.3.1.html> I'm using
     * the address of a static pointer as a unique key into the registry table
     */
    lua_pushlightuserdata(L, (void *)&EXECUTOR_LUA_REGISTRY_KEY); /* push address */
    lua_pushlightuserdata(L, (void*)self); /* push value */
    /* registry[&EXECUTOR_LUA_REGISTRY_KEY] = self */
    lua_settable(L, LUA_REGISTRYINDEX);

    /*
     * set up the metatable for userdata objects for calling Python functions
     * from Lua
     */
    luaL_newmetatable(L, EXECUTOR_LUA_FUNCTION_MT_KEY);
    // add the __gc method so we can clean them up
    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, free_python_function);
    lua_settable(L, -3);
    // and make them callable
    lua_pushstring(L, "__call");
    lua_pushcfunction(L, call_python_function_from_lua);
    lua_settable(L, -3);
    lua_pop(L, 1); // get the metatable off of the stack

    self->limit_allocation = TRUE;

    self->L = L;

    goto done;

error:
    ret = -1;

    if(have_lock) {
        pthread_mutex_destroy(&self->l_mutex);
    }

    if(L != NULL) {
        lua_close(L);
    }

done:
    return ret;
}

static void _LuaExecutor_dealloc(_LuaExecutor* self) {
    if(self->L != NULL) {
        /* may be NULL if we died during the constructor */
        lua_close(self->L);
    }
    pthread_mutex_destroy(&self->l_mutex);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject* _LuaExecutor__stack_top(_LuaExecutor* self) {
    return PyInt_FromLong(lua_gettop(self->L));
}

static void dealloc_lua_capsule(PyObject* capsule) {
    _LuaExecutor* self = (_LuaExecutor*)PyCapsule_GetContext(capsule);
    Py_XDECREF(self);
}

static PyObject* _LuaExecutor__get_lua(_LuaExecutor* self) {
    PyObject* capsule = PyCapsule_New((void*)(self->L),
                                      EXECUTOR_CAPSULE_NAME,
                                      dealloc_lua_capsule);
    if(capsule == NULL) {
        return NULL;
    }
    PyCapsule_SetContext(capsule, (void*)self);
    Py_INCREF(self); // it holds a reference to us
    return capsule;
}

static PyObject* _LuaExecutor_execute(_LuaExecutor* self, PyObject* args) {
    PyObject* env = NULL;
    PyObject* pyresult = NULL;

    char* program_code = NULL;
    size_t program_len = 0;

    const char* err_string;
    size_t err_string_len;

    if(!PyArg_ParseTuple(args, "s#O!",
                         &program_code, &program_len,
                         &PyDict_Type, &env)) {
        return NULL;
    }

    lua_State* L = self->L;

    if(gettimeofday(&self->script_started, NULL)) {
        PyErr_SetString(PyExc_RuntimeError, "error building time quota checker");
        return NULL;
    }

    // turn off during serialize_python_to_lua
    self->limit_allocation = FALSE;

    /* if we got an environment to pass in, translate it from Python to Lua */
    if(PyDict_Size(env)>=0 && !serialize_python_to_lua(L, env, self->max_recursion)) {
        /* there will be an error on the Python stack */
        goto done;
    }

    /* Load the the script we are going to run */
    if (luaL_loadbufferx(L, program_code, program_len, "_LuaExecutor", "t")) {
        /* If something went wrong, error message is at the top of the stack */

        /* copy the errstring out into a Python string */
        err_string = lua_tolstring(L, -1, &err_string_len);
        PyObject* pyerrstring = PyString_FromStringAndSize(err_string,
                                                           err_string_len);
         lua_pop(L, 1);
         if(!pyerrstring) {
            goto done;
        }

        PyErr_SetObject(LuaException, pyerrstring);

        goto done;
    }

    int stack_top_before = lua_gettop(L);
    int lua_result;

    // TODO check the return value here
    pthread_mutex_lock(&self->l_mutex);
    Py_BEGIN_ALLOW_THREADS;

    // turn on while running the user code. because we use pcall, allocation
    // failures will return back to us here instead of longjmping out into space
    self->limit_allocation = TRUE;

    /* Ask Lua to run our script */
    lua_result = lua_pcall(L, 0, LUA_MULTRET, 0);

    Py_END_ALLOW_THREADS;
    // TODO check the return value here
    pthread_mutex_unlock(&self->l_mutex);

    if(lua_result != LUA_OK) {
        /* If something went wrong, error message is at the top of the stack */

        /* copy the errstring out */
        err_string = lua_tolstring(L, -1, &err_string_len);
        PyObject* pyerrstring = PyString_FromStringAndSize(err_string,
                                                           err_string_len);
        lua_pop(L, 1);
         if(!pyerrstring) {
            // what can we even do with this
            goto done;
        }

        if(lua_result == LUA_ERRMEM) {
            PyErr_SetObject(LuaOutOfMemoryException, pyerrstring);
        } else {
            PyErr_SetObject(LuaException, pyerrstring);
        }

        goto done;
    }

    /*
     * lua removes from the stack the code chunk that we loaded and the
     * arguments to it (which we didn't pass), and leaves the return values on
     * it afterwards.
     * if there was an error, it's signalled in lua_result and the error message
     * left instead. in that case, we'll extract it and return NULL and leave an
     * exception on the Python stack
     */

    int stack_top_after = lua_gettop(L);
    int results_returned = 1+stack_top_after-stack_top_before;

    pyresult = serialize_lua_to_python_multi(L,
                                             stack_top_before,
                                             results_returned,
                                             self->max_recursion);

    if(pyresult==NULL) {
        goto error;
    }

    lua_pop(L, results_returned);
    goto done;

error:
    Py_XDECREF(pyresult);
    lua_pop(L, results_returned);

done:
    // make sure this gets set back on
    self->limit_allocation = TRUE;

    if(PyErr_Occurred()) {
        return NULL;
    }

    return pyresult;
}

static void stackDump(lua_State *L) {
    // http://www.lua.org/pil/24.2.3.html
    int top = lua_gettop(L);

    for(int i = 1; i <= top; i++) {  /* repeat for each level */
        int t = lua_type(L, i);
        switch (t) {
            case LUA_TSTRING:  /* strings */
                printf("`%s'", lua_tostring(L, i));
                break;

            case LUA_TBOOLEAN:  /* booleans */
                printf(lua_toboolean(L, i) ? "true" : "false");
                break;

            case LUA_TNUMBER:  /* numbers */
                printf("%g", lua_tonumber(L, i));
                break;

            default:  /* other values */
                printf("%s", lua_typename(L, t));
                break;
        }
        printf("  ");  /* put a separator */
    }
    printf("\n");  /* end the listing */
}
