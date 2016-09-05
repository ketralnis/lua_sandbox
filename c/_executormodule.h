#ifndef _EXECUTOR_MODULE_H
#define _EXECUTOR_MODULE_H

#include <time.h>

#include <Python.h>

#if PY_VERSION_HEX < 0x02070000
#error "We require Python 2.7 to run"
#endif

#ifndef LUA_OK
// 5.1 doesn't define this
#define LUA_OK 0
#endif

char* EXECUTOR_LUA_CALLABLE_KEY = "EXECUTOR_LUA_CALLABLE_KEY";
char* EXECUTOR_MEMORY_LIMITER_KEY = "EXECUTOR_MEMORY_LIMITER_KEY";
char* EXECUTOR_RUNTIME_LIMITER_KEY = "EXECUTOR_RUNTIME_LIMITER_KEY";
char* EXECUTOR_JMP_RETURN_KEY = "EXECUTOR_JMP_RETURN_KEY";

// mirrored in executor.py
typedef struct {
    int limit_allocation;
    size_t memory_used;
    size_t memory_limit;
#if LUA_VERSION_NUM == 501
    lua_Alloc old_allocf;
    void* old_ud;
#endif
} l_alloc_limiter;

typedef struct {
    clock_t start;
    double max_runtime;
    clock_t expires;
}  l_runtime_limiter;

typedef struct {
    PyObject* executor;
    PyObject* callable;
    PyObject* val;
    long cycle_key;
    PyDictObject* cycles;
} python_callable;

PyMODINIT_FUNC init_executor(void);

int call_python_function_from_lua(lua_State *L);
int free_python_callable(lua_State *L);
void retain_python_callable(PyObject* callable);
void store_python_callable(lua_State*,PyObject*,PyObject*,PyObject*,long,PyDictObject*);

void* l_alloc_restricted (l_alloc_limiter*, void *, size_t, size_t);
l_alloc_limiter* new_memory_limiter(lua_State* L, size_t max_memory);
void free_memory_limiter(lua_State *L, l_alloc_limiter* limiter);
void enable_limit_memory(l_alloc_limiter *limiter);
void disable_limit_memory(l_alloc_limiter *limiter);

l_runtime_limiter* new_runtime_limiter(lua_State *L, double);
void free_runtime_limiter(lua_State *L, l_runtime_limiter*);
void time_limiting_hook(lua_State *L, lua_Debug *ar);

#if LUA_VERSION_NUM == 501
int memory_safe_pcallk(lua_State *L, int nargs, int nresults, int _msgh);
#endif

#endif /* _EXECUTOR_MODULE_H */
