#include <Python.h>

#include "_executormodule.h"

extern PyTypeObject _LuaExecutorType;

// we fill these on module init
PyObject *LuaException = NULL;
PyObject *LuaOutOfMemoryException = NULL;

PyMODINIT_FUNC init_executor(void) {
    // initialise the module

    PyObject* module = NULL;

    module = Py_InitModule3("lua_sandbox._executor",
        NULL, /* no functions of our own */
        "C module that implements the Lua-Python bridge");
    if (module == NULL) {
        /* exception raised in preparing */
        return;
    }

    LuaException = PyErr_NewException("_executor.LuaException", NULL, NULL);
    if(LuaException == NULL) {
        goto error;
    }
    Py_INCREF(LuaException);
    PyModule_AddObject(module, "LuaException", LuaException);

    LuaOutOfMemoryException = PyErr_NewException("_executor.LuaOutOfMemoryException", LuaException, NULL);
    if(LuaOutOfMemoryException == NULL) {
        goto error;
    }
    Py_INCREF(LuaOutOfMemoryException);
    PyModule_AddObject(module, "LuaOutOfMemoryException", LuaOutOfMemoryException);

    // have to do this here because some C compilers have issues with static
    // references between modules. we can take this out when we make our own
    _LuaExecutorType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&_LuaExecutorType) < 0) {
        /* exception raised in preparing */
        goto error;
    }

    /* make it visible */
    Py_INCREF(&_LuaExecutorType);
    PyModule_AddObject(module, "_LuaExecutor", (PyObject *)&_LuaExecutorType);

    return;

error:
    Py_XDECREF(module);
    Py_XDECREF(LuaException);
    Py_XDECREF(LuaOutOfMemoryException);

    return;
}
