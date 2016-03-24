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

    if (self->memory_used + (nsize - osize) > self->memory_limit) {
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

    if(diff >= 1000*1000) {
        lua_pushstring(L, "time quota exceeded");
        lua_error(L);
    }
}

int encode_python_to_lua(lua_State* L, PyObject* value,
                         int recursion, int max_recursion) {
    if(recursion > max_recursion) {
        PyErr_SetString(PyExc_RuntimeError, "encode_python_to_lua recursed too far");
        return 0;

    } else if(!lua_checkstack(L, 1)) {
        PyErr_SetString(PyExc_TypeError, "not enough lua stack space");
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

    } else {
        format_python_exception(PyExc_TypeError,
                                "cannot serialize unknown python type of %r",
                                value, NULL);
        return 0;
    }

    return 1;
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
            /* TODO: put the key in the error message */
            PyErr_SetString(PyExc_TypeError, "key must be str");
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
    idx = abs_index(L, idx);

    if(recursion > max_recursion) {
        PyErr_SetString(PyExc_RuntimeError, "serialize_lua_to_python recursed too far");
        return NULL;
    }

    switch(lua_type(L, idx)) {

    case LUA_TNIL:
        ret = Py_None;
        Py_INCREF(ret);
        break;

    case LUA_TNUMBER:
        as_double = lua_tonumber(L, idx);
        ret = PyFloat_FromDouble(as_double);
        if(ret == NULL) {
            return NULL;
        }
        break;

    case LUA_TBOOLEAN:
        as_boolean = lua_toboolean(L, idx);
        if(as_boolean) {
            ret = Py_True;
        } else {
            ret = Py_False;
        }
        Py_INCREF(ret);
        break;

    case LUA_TSTRING:
        string = lua_tolstring(L, idx, &string_len);
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

        int table_index = abs_index(L, idx);

        lua_pushnil(L);  /* first key */

        while (lua_next(L, table_index) != 0) {
            /* `key' is at index -2 and `value' at index -1 */
            PyObject* key = serialize_lua_to_python(L, -2,
                                                    recursion+1, max_recursion);
            if(key == NULL) {
                Py_DECREF(ret);
                return NULL;
            }

            PyObject* value = serialize_lua_to_python(L, -1,
                                                      recursion+1, max_recursion);
            if(value == NULL) {
                Py_DECREF(key);
                Py_DECREF(ret);
                return NULL;
            }

            if(PyDict_SetItem(ret, key, value) == -1) {
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

        break;

    default:
        PyErr_Format(PyExc_RuntimeError,
                     "cannot serialize unknown Lua type %s",
                     lua_typename(L, lua_type(L, idx)));
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
     * this is about one second on my laptop. TODO investigate what it would
     * take to make this into seconds instead of cycles
     */
    unsigned long long max_lua_cycles = 500000;
    /*
     * max depth of serialisation/deserialisation
     */
    unsigned long max_lua_depth = 10;

    static char *kwdlist[] = {"max_memory", "max_cycles", "max_object_depth"};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
                                     "|nKk",
                                     kwdlist,
                                     &max_lua_allocation,
                                     &max_lua_cycles,
                                     &max_lua_depth)) {
        return -1;
    }

    int have_lock = 0;

    self->memory_used = 0;
    self->memory_limit = max_lua_allocation;
    self->max_recursion = max_lua_depth;

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
    lua_sethook(L, time_limiting_hook, LUA_MASKCOUNT, max_lua_cycles);

    /*
     * add a pointer back to the _LuaExecutor object within the lua_State.
     * following the advice of <http://www.lua.org/pil/27.3.1.html> I'm using
     * the address of a static pointer as a unique key into the registry table
     */
    lua_pushlightuserdata(L, (void *)&EXECUTOR_LUA_REGISTRY_KEY); /* push address */
    lua_pushlightuserdata(L, (void*)self); /* push value */
    /* registry[&EXECUTOR_LUA_REGISTRY_KEY] = self */
    lua_settable(L, LUA_REGISTRYINDEX);

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

static PyObject* _LuaExecutor_execute(_LuaExecutor* self, PyObject* args) {
    PyObject* env;
    PyObject* pyresult;

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

        PyErr_SetObject(PyExc_RuntimeError, pyerrstring);

        goto done;
    }

    int stack_top_before = lua_gettop(L);
    int lua_result;

    // TODO check the return value here
    pthread_mutex_lock(&self->l_mutex);
    Py_BEGIN_ALLOW_THREADS;

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

        PyErr_SetObject(PyExc_RuntimeError, pyerrstring);

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

    pyresult = PyTuple_New(results_returned);

    if(pyresult==NULL) {
        goto done;
    }

    if(results_returned==0) {
        // success, return the empty tuple
        goto done;
    }

    for(int i=0; i<results_returned; i++) {
        int stacknum = stack_top_before+i;
        PyObject* thisresult = serialize_lua_to_python(L, stacknum,
                                                       0, self->max_recursion);
        if(thisresult==NULL) {
            goto error;
        }
        // steals the thisresult ref, even if it fails
        if(PyTuple_SetItem(pyresult, i, thisresult)==-1) {
            goto error;
        }
    }

    lua_pop(L, results_returned);
    goto done;

error:
    if(pyresult != NULL) {
        Py_DECREF(pyresult);
    }
    lua_pop(L, results_returned);

done:
    if(PyErr_Occurred()) {
        return NULL;
    }

    return pyresult;
}
