#ifndef _EXECUTOR_MODULE_H
#define _EXECUTOR_MODULE_H

#include <time.h>
#include <setjmp.h>

#include <Python.h>

#if PY_VERSION_HEX < 0x02070000
#error "We require Python 2.7 to run"
#endif

#ifndef LUA_OK
// 5.1 doesn't define this
#define LUA_OK 0
#endif

char* EXECUTOR_LUA_CAPSULE_KEY = "EXECUTOR_LUA_CAPSULE_KEY";

typedef struct {
    int enabled;
    size_t memory_used;
    size_t memory_limit;
    lua_Alloc old_allocf;
    void* old_ud;
} memory_limiter;

typedef struct {
    int enabled;
    clock_t start;
    clock_t expires;
    double max_runtime;
} runtime_limiter;

typedef struct {
    PyObject* val;
    int cache_ref;
    int cache;
    int recursive;
    int raw_lua_args;
} lua_capsule;

typedef struct {
    memory_limiter memory;
    runtime_limiter runtime;
    PyObject* references;
#if LUA_VERSION_NUM == 501
    jmp_buf* panic_return;
#endif
} lua_control_block;

PyMODINIT_FUNC PyInit__executor(void);

int install_control_block(lua_State *L, size_t max_memory,
                          PyObject* references);
void wrapped_lua_close(lua_State*);
void start_runtime_limiter(lua_State*, double max_runtime, int hz);
void finish_runtime_limiter(lua_State*);
static void time_limiting_hook(lua_State*, lua_Debug *_ar);
void* l_alloc_restricted (lua_control_block*,void*, size_t, size_t);
size_t get_memory_used(lua_State *L);
void enable_limit_memory(lua_State *L);
void disable_limit_memory(lua_State *L);
int call_python_function_from_lua(lua_State *L);
void store_python_capsule(lua_State*,PyObject*,int,int,int);
int free_python_capsule(lua_State *L);
PyObject* decapsule(lua_capsule* capsule);
int lazy_capsule_index(lua_State*);
PyObject* lua_string_to_python_buffer(lua_State*, int idx);
static int check_capsule_cache(lua_State* L, lua_capsule*, int);
static void set_capsule_cache(lua_State* L, lua_capsule*, int, int);
static void create_capsule_cache(lua_State* L, lua_capsule*);
static int translate_python_exception(lua_State*, PyGILState_STATE);

#if LUA_VERSION_NUM == 501
static int memory_panicer (lua_State *L);
int memory_safe_pcallk(lua_State *L, int nargs, int nresults, int _msgh);
#endif

#endif /* _EXECUTOR_MODULE_H */
