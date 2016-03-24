#include <Python.h>

#include "_executormodule.h"

extern PyTypeObject _LuaExecutorType;

PyMODINIT_FUNC init_executor(void) {
    // initialise the module

    PyObject* module;

    // have to do this here because some C compilers have issues with static
    // references between modules. we can take this out when we make our own
    _LuaExecutorType.tp_new = PyType_GenericNew;

    if (PyType_Ready(&_LuaExecutorType) < 0) {
        /* exception raised in preparing */
        return;
    }

    module = Py_InitModule3("lua_sandbox._executor",
        NULL, /* no functions of our own */
        "C module that implements the Lua-Python bridge");

    if (module == NULL) {
        /* exception raised in preparing */
        return;
    }

    /* make it visible */
    Py_INCREF(&_LuaExecutorType);
    PyModule_AddObject(module, "_LuaExecutor", (PyObject *)&_LuaExecutorType);
}
