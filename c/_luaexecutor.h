#ifndef _LUA_EXECUTOR_H
#define _LUA_EXECUTOR_H

#include <pthread.h>
#include <structmember.h>

#include <Python.h>

#include <lua.h>

/*
 * the libraries require about 100k to run on their own, give them that plus
 * some breathing room
 */
const size_t MAX_LUA_ALLOCATION=2*1024*1024;
const int MAX_LUA_EXECUTION_HZ=500000;
const int MAX_LUA_DEPTH=10;

static const char* EXECUTOR_LUA_REGISTRY_KEY = "_LuaExecutor";

typedef struct {
    PyObject_HEAD
    /* our own C-visible fields go here. */

    /* the lua VM we're using */
    lua_State *L;

    /*
     * lua doesn't lock itself, so we're expected to do that. The rule that we
     * follow is that we must lock this any time that we release the GIL but
     * want to use the lua_State
     */
    pthread_mutex_t l_mutex;

    struct timeval script_started;

    /* for the custom allocator */
    size_t memory_limit;
    size_t memory_used;

} _LuaExecutor;

/* prototypes */
static void format_python_exception(PyObject* exc_type, const char *fmt, ...);
static int _LuaExecutor_init(_LuaExecutor *self, PyObject *args, PyObject *kwds);
static void _LuaExecutor_dealloc(_LuaExecutor* self);
static PyObject* _LuaExecutor_execute(_LuaExecutor* self, PyObject* args);
static PyObject* _LuaExecutor__stack_top(_LuaExecutor* self);

/* the method table */
static PyMethodDef _LuaExecutorType_methods[] = {
    /* Python-visible methods go here */
    {
        "execute",
        (PyCFunction)_LuaExecutor_execute, METH_VARARGS,
        "execute the passed code with the passed environment"
    },
    {
        "_stack_top",
        (PyCFunction)_LuaExecutor__stack_top, METH_NOARGS,
        "private function to look at how big the internal stack is, mostly for tests"
    },
    {NULL, NULL, 0, NULL}
};

static PyMemberDef _LuaExecutorType_members[] = {
    /* we handle them all internally for now */
    {NULL}
};

/* the actual class */
PyTypeObject _LuaExecutorType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /* ob_size */
    "_executor._LuaExecutor",  /* tp_name */
    sizeof(_LuaExecutor),      /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)_LuaExecutor_dealloc,  /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE, /* tp_flags */
    "The Lua executor client. Don't use me directly, use lua_sandbox.executor.LuaExecutor.", /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    _LuaExecutorType_methods,  /* tp_methods */
    _LuaExecutorType_members,  /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)_LuaExecutor_init,  /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new, will set in init_memcev */
};

#endif /* _LUA_EXECUTOR_H */
