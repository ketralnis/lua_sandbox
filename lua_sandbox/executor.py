from ctypes.util import find_library
from functools import partial
from functools import wraps
import contextlib
import ctypes

from lua_sandbox import _executor
from lua_sandbox.utils import datafile, dataloc

lua_lib_location = find_library(_executor.LUA_LIB_NAME)
lua_lib = ctypes.PyDLL(lua_lib_location)
lua_lib_nogil = ctypes.CDLL(lua_lib_location)

if not lua_lib_location or not lua_lib.lua_newstate:
    raise ImportError("unable to locate lua (%r)" % _executor.LUA_LIB_NAME)

# we talk to executor in two ways: as a Python module and as a ctypes module
executor_lib_location = dataloc('_executor.so')
executor_lib = ctypes.PyDLL(executor_lib_location)
executor_lib_nogil = ctypes.CDLL(executor_lib_location)

if not executor_lib or not executor_lib.l_alloc_restricted:
    raise ImportError("unable to locate executor")

if _executor.EXECUTOR_LUA_NUMBER_TYPE_NAME == 'double':
    lua_number_type = ctypes.c_double
elif _executor.EXECUTOR_LUA_NUMBER_TYPE_NAME == 'float':
    lua_number_type = ctypes.c_float
else:
    raise ImportError("Unable to deal with lua configured with LUA_NUMBER=%s"
                    % _executor.EXECUTOR_LUA_NUMBER_TYPE_NAME)

# dylib exports
luaL_loadbufferx = lua_lib.luaL_loadbufferx
luaL_newmetatable = lua_lib.luaL_newmetatable
luaL_newmetatable.restype = ctypes.c_int
luaL_newstate = lua_lib.luaL_newstate
luaL_newstate.restype = ctypes.c_void_p
luaL_openlibs = lua_lib.luaL_openlibs
luaL_openlibs.restype = None
luaL_ref = lua_lib.luaL_ref
luaL_ref.restype = ctypes.c_int
luaL_unref = lua_lib.luaL_unref
luaL_unref.restype = ctypes.c_int
lua_checkstack = lua_lib.lua_checkstack
lua_checkstack.restype = ctypes.c_int
lua_createtable = lua_lib.lua_createtable
lua_createtable.restype = None
lua_gc = lua_lib_nogil.lua_gc
lua_gc.restype = ctypes.c_int
lua_getallocf = lua_lib.lua_getallocf
lua_getallocf.restype = ctypes.c_void_p
lua_getfield = lua_lib.lua_getfield
lua_getfield.restype = None
lua_getmetatable = lua_lib.lua_getmetatable
lua_getmetatable.restype = ctypes.c_int
lua_gettop = lua_lib.lua_gettop
lua_gettop.restype = ctypes.c_int
lua_newstate = lua_lib.lua_newstate
lua_newstate.restype = ctypes.c_void_p
lua_next = lua_lib.lua_next
lua_pushboolean = lua_lib.lua_pushboolean
lua_pushboolean.restype = None
lua_pushcclosure = lua_lib.lua_pushcclosure
lua_pushcclosure.restype = None
lua_pushlightuserdata = lua_lib.lua_pushlightuserdata
lua_pushlightuserdata.restype = None
lua_pushlstring = lua_lib.lua_pushlstring
lua_pushlstring.restype = ctypes.c_void_p # this isn't true but we never use it
lua_pushnil = lua_lib.lua_pushnil
lua_pushnil.restype = None
lua_pushnumber = lua_lib.lua_pushnumber
lua_pushnumber.restype = None
lua_pushstring = lua_lib.lua_pushstring
lua_pushstring.restype = ctypes.c_void_p # this isn't true but we never use it
lua_rawget = lua_lib.lua_rawget
lua_rawget.restype = None
lua_rawgeti = lua_lib.lua_rawgeti
lua_rawgeti.restype = None
lua_rawset = lua_lib.lua_rawset
lua_rawset.restype = None
lua_rawseti = lua_lib.lua_rawseti
lua_rawseti.restype = None
lua_setallocf = lua_lib.lua_setallocf
lua_setallocf.restype = None
lua_setfield = lua_lib.lua_setfield
lua_setfield.restype = ctypes.c_int
lua_setmetatable = lua_lib.lua_setmetatable
lua_setmetatable.restype = None
lua_settop = lua_lib.lua_settop
lua_settop.restype = None
lua_setupvalue = lua_lib.lua_setupvalue
lua_setupvalue.restype = ctypes.c_void_p # this isn't true but we never use it
lua_toboolean = lua_lib.lua_toboolean
lua_tolstring = lua_lib.lua_tolstring
lua_tolstring.restype = ctypes.c_char_p
lua_touserdata = lua_lib.lua_touserdata
lua_touserdata.restype = ctypes.c_void_p
lua_type = lua_lib.lua_type
lua_typename = lua_lib.lua_typename
lua_typename.restype = ctypes.c_char_p


EXECUTOR_LUA_CAPSULE_KEY = ctypes.c_char_p.in_dll(executor_lib,
    "EXECUTOR_LUA_CAPSULE_KEY")
install_control_block = executor_lib.install_control_block
install_control_block.restype = ctypes.c_int
wrapped_lua_close = executor_lib_nogil.wrapped_lua_close
wrapped_lua_close.restype = None
start_runtime_limiter = executor_lib.start_runtime_limiter
start_runtime_limiter.restype = None
finish_runtime_limiter = executor_lib.finish_runtime_limiter
finish_runtime_limiter.restype = None
get_memory_used = executor_lib.get_memory_used
get_memory_used.restype = ctypes.c_size_t
enable_limit_memory = executor_lib.enable_limit_memory
enable_limit_memory.restype = None
disable_limit_memory = executor_lib.disable_limit_memory
disable_limit_memory.restype = None
call_python_function_from_lua = executor_lib.call_python_function_from_lua
call_python_function_from_lua.restype = ctypes.c_int
store_python_capsule = executor_lib.store_python_capsule
store_python_capsule.restype = None
free_python_capsule = executor_lib.free_python_capsule
free_python_capsule.restype = ctypes.c_int
decapsule = executor_lib.decapsule
decapsule.restype = ctypes.py_object
lazy_capsule_index = executor_lib.lazy_capsule_index
lazy_capsule_index.restype = ctypes.c_int

# function types
lua_CFunction = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)


# this is normally a macro from lua.h
def lua_pop(L, n):
    return lua_settop(L, -(n)-1)


def luaL_getmetatable(L, n):
    return lua_getfield(L, _executor.LUA_REGISTRYINDEX, n)


def lua_isnil(L, idx):
    return lua_type(L, idx) == _executor.LUA_TNIL


if _executor.LUA_VERSION_NUM == 501:
    lua_pcallk = executor_lib_nogil.memory_safe_pcallk

    lua_tonumber = lua_lib.lua_tonumber
    lua_tonumber.restype = lua_number_type
    def lua_tonumberx(L, idx, _):
        return lua_tonumber(L, idx, None)

    lua_setfenv = lua_lib.lua_setfenv

    def lua_getglobal(L, s):
        return lua_getfield(L, _executor.LUA_GLOBALSINDEX, s)

    def lua_setglobal(L, s):
        return lua_setfield(L, _executor.LUA_GLOBALSINDEX, s)

    luaJIT_setmode = lua_lib.luaJIT_setmode

elif _executor.LUA_VERSION_NUM in (502, 503):
    lua_pcallk = lua_lib_nogil.lua_pcallk
    lua_tonumberx = lua_lib.lua_tonumberx
    lua_tonumberx.restype = lua_number_type
    lua_getglobal = lua_lib.lua_getglobal
    lua_getglobal.restype = None
    lua_setglobal = lua_lib.lua_setglobal
    lua_setglobal.restype = None

else:
    raise ImportError("I don't know LUA_VERSION_NUM %r", _executor.LUA_VERSION_NUM)


def abs_index(L, i, LUA_REGISTRYINDEX=_executor.LUA_REGISTRYINDEX):
    "convert a potentially relative stack index to an absolute one"
    if i > 0 or i <= LUA_REGISTRYINDEX:
        return i
    return lua_gettop(L) + i + 1


def check_stack(needs=0, expected=0):
    """
    double check that the function affects the lua stack the way we think it
    does
    """

    def wrap(fn):
        @wraps(fn)
        def wrapped(self, *a, **kw):
            if needs and not lua_checkstack(self.L, needs):
                raise LuaOutOfMemoryException("%r.checkstack" % (fn,))

            oom = False
            before_top = lua_gettop(self.L)

            try:
                return fn(self, *a, **kw)
            except LuaOutOfMemoryException:
                # if we ran out of ram the whole instance is probably blown so
                # the stack probably can't be cleaned up
                oom = True
                raise
            finally:
                if not oom:
                    # this will override any real exception being thrown, but it
                    # indicates a bug in the library so fixing that is probably
                    # more important than returning values from a VM that we're
                    # now unsure about the state of
                    after_top = lua_gettop(self.L)
                    if after_top-before_top != expected:
                        raise LuaInvariantException(
                            ("check_stack in %r"
                             "(needs=%d/expected=%d/before_top=%d,after_top=%d)")
                            % (fn, needs, expected, before_top, after_top))

        return wrapped
    return wrap


# the libraries require about 100k to run on their own. The default of
# 2mb gives them that plus some breathing room. 0 to disable
MAX_MEMORY_DEFAULT = 2*1024*1024

MAX_RUNTIME_DEFAULT = 2.0 # (in seconds)
MAX_RUNTIME_HZ_DEFAULT = 500*1000 # how often to check (in "lua instructions")

class Lua(object):
    __slots__ = ['L', 'max_memory', 'cleanup_cache', 'name', 'cycles']

    def __init__(self, max_memory=MAX_MEMORY_DEFAULT, name=None):
        self.name = name or "%s[%s]" % (self.__class__.__name__, id(self))

        self.max_memory = max_memory = max_memory or 0

        # If every time we hold a reference in Lua land to an object in Python
        # land we do the obvious incref/decref pair we end up with reference
        # cycles which prevent those objects from ever getting cleaned up
        # resulting in terrible memory leaks. So instead we use a combination
        # of Lua refs and the registry and this dictionary of references. When
        # we create a reference to a Python object, we add it to this
        # dictionary. Then when Lua removes its last references, the __gc
        # method calls back into Python to remove it from this dictionary.
        # This way we allow Python's cycle-detecting GC full access to
        # something that more closely resembles the actual object graph. lupa
        # uses the same technique
        # https://github.com/scoder/lupa/commit/c634e44d77a17adcf99a562284da76a5a40065a4
        self.cycles = {}

        self.L = luaL_newstate()
        self.L = ctypes.c_void_p(self.L)  # save us casts later

        if not install_control_block(self.L, ctypes.c_size_t(max_memory)):
            raise LuaOutOfMemoryException("couldn't allocate control block")

        luaL_openlibs(self.L)
        self.install_python_capsule()

        # hold on to this for __del__
        self.cleanup_cache = dict(
            wrapped_lua_close = wrapped_lua_close,
        )

    def __repr__(self):
        return "<%s %s>" % (self.__class__.__name__, self.name)

    @contextlib.contextmanager
    def limit_runtime(self,
                      max_runtime=MAX_RUNTIME_DEFAULT,
                      max_runtime_hz=MAX_RUNTIME_HZ_DEFAULT):

        start_runtime_limiter(self.L,
                              ctypes.c_double(max_runtime),
                              ctypes.c_int(max_runtime_hz))

        try:
            yield
        finally:
            finish_runtime_limiter(self.L)

    @check_stack(3, 0)
    def install_python_capsule(self):
        # we create a global metatable to act as a prototype that contains our
        # methods
        luaL_newmetatable(self.L, EXECUTOR_LUA_CAPSULE_KEY)

        # we need to be able to dealloc them
        lua_pushlightuserdata(self.L, ctypes.py_object(self.cycles))
        lua_pushcclosure(self.L, free_python_capsule, 1)
        lua_setfield(self.L, -2, '__gc')

        # and call them
        lua_pushlightuserdata(self.L, ctypes.py_object(_callable_wrapper))
        lua_pushlightuserdata(self.L, ctypes.py_object(self))
        lua_pushcclosure(self.L, call_python_function_from_lua, 2)
        lua_setfield(self.L, -2, '__call')

        # and index them
        lua_pushlightuserdata(self.L, ctypes.py_object(_indexable_wrapper))
        lua_pushlightuserdata(self.L, ctypes.py_object(self))
        lua_pushcclosure(self.L, lazy_capsule_index, 2)
        lua_setfield(self.L, -2, '__index')

        lua_pushstring(self.L, "capsule")
        lua_setfield(self.L, -2, "capsule")

        lua_pop(self.L, 1)  # get the metatable off the stack

    def gc(self):
        "Force a garbage collection"
        lua_gc(self.L, _executor.LUA_GCCOLLECT, 0)

    @check_stack(2, 0)
    def registry(self, key):
        key = LuaValue.from_python(self, key)
        key._bring_to_top(False)
        lua_rawget(self.L, _executor.LUA_REGISTRYINDEX)
        return LuaValue(self)

    @check_stack(1, 0)
    def set_global(self, key, value):
        if not isinstance(key, str):
            raise TypeError("key must be str, not %r" % (key,))

        lvalue = LuaValue.from_python(self, value)
        lvalue._bring_to_top(False)
        lua_setglobal(self.L, key)

    def __setitem__(self, key, value):
        return self.set_global(key, value)

    def __delitem__(self, key):
        self[key] = None

    @check_stack(1, 0)
    def get_global(self, key):
        if not isinstance(key, str):
            raise TypeError("key must be str, not %r" % (key,))

        lua_getglobal(self.L, key)
        return LuaValue(self)

    def __getitem__(self, key):
        return self.get_global(key)

    @property
    def memory_used(self):
        return get_memory_used(self.L)

    @check_stack(1, 0)
    def load(self, code, desc=None, mode="t"):
        assert isinstance(code, str)
        assert isinstance(desc, (type(None), str))

        load_ret = luaL_loadbufferx(self.L,
                                    code,
                                    ctypes.c_size_t(len(code)),
                                    desc or self.__class__.__name__,
                                    mode)

        if load_ret == _executor.LUA_OK:
            return LuaValue(self)
        elif load_ret == _executor.LUA_ERRSYNTAX:
            raise LuaSyntaxError(self)
        elif load_ret == _executor.LUA_ERRMEM:
            raise LuaOutOfMemoryException('load')
        else:
            raise LuaStateException(self)

    @check_stack(1, 0)
    def create_table(self):
        lua_createtable(self.L, 0, 0)
        return LuaValue(self)

    def __del__(self):
        if self.L:
            self.cleanup_cache['wrapped_lua_close'](self.L)


class LuaValue(object):
    __slots__ = ['executor', 'L', 'key', 'cleanup_cache']

    def __init__(self, executor, cycle_id=None):
        """
        Build a LuaValue off of whatever's on the top of the stack, then pop it
        off. We store it in Lua's global
        """

        assert isinstance(executor, Lua), "expected Lua, got %r" % (executor,)

        # it's important that we keep a reference to the executor because he's
        # the one that keeps the lua_State* live
        self.executor = executor
        self.L = executor.L
        self._create(cycle_id)

    @check_stack(2, -1)
    def _create(self, cycle_id):
        # right now the top of the stack contains the item that we're storing

        # pops it off and stores the integer reference in the registry
        self.key = luaL_ref(self.L, _executor.LUA_REGISTRYINDEX)

        # now we've consumed it from the stack

        # this is a little weird, but we need to hold on to these so that our
        # __del__ has access to them
        self.cleanup_cache = dict(
            luaL_unref=luaL_unref,
            LUA_REGISTRYINDEX=_executor.LUA_REGISTRYINDEX,
        )

        if cycle_id is not None:
            id_, val = cycle_id
            # added here, removed in _executormodule.c:free_python_capsule
            self.executor.cycles.setdefault(id_, []).append(val)

    def __del__(self):
        self.cleanup_cache['luaL_unref'](self.L,
            self.cleanup_cache['LUA_REGISTRYINDEX'],
            self.key)

    @check_stack()
    def type_name(self):
        with self._bring_to_top():
            # extract the value
            name = lua_typename(self.L, lua_type(self.L, -1))

        return name

    @contextlib.contextmanager
    def _bring_to_top(self, cleanup_after=True):
        "Get the value to the top of the stack"
        if not lua_checkstack(self.L, 1):
            raise LuaOutOfMemoryException("_bring_to_top.checkstack")

        # get the value to the top of the stack
        lua_rawgeti(self.L, _executor.LUA_REGISTRYINDEX, self.key)

        if cleanup_after:
            return self.__bring_to_top()

    def __bring_to_top(self):
        try:
            yield
        finally:
            lua_pop(self.L, 1)

    def _to_python(self, idx):
        kind = lua_type(self.L, idx)

        if kind == _executor.LUA_TNIL:
            return None

        elif kind == _executor.LUA_TBOOLEAN:
            return bool(lua_toboolean(self.L, idx, None))

        elif kind == _executor.LUA_TNUMBER:
            return lua_tonumberx(self.L, idx, None)

        elif kind == _executor.LUA_TSTRING:
            size = ctypes.c_size_t(0)
            as_char_p = lua_tolstring(self.L, idx, ctypes.byref(size))
            # since that's a ptr into Lua state we need to copy it out
            as_string = ctypes.string_at(as_char_p, size.value)
            return as_string

        elif kind == _executor.LUA_TTABLE:
            ret = {}

            idx = abs_index(self.L, idx)

            lua_pushnil(self.L) # first key

            while True:
                nidx = lua_next(self.L, idx)

                if nidx == 0:
                    break

                # `key' is at index -2 and `value' at index -1
                try:
                    value = self._to_python(-1)
                    key = self._to_python(-2)
                except Exception:
                    lua_pop(self.L, 2)
                    raise

                ret[key] = value

                # removes value, leaves key for next iteration
                lua_pop(self.L, 1)

            return ret
        elif kind == _executor.LUA_TFUNCTION:
            return self # we're already callable

        elif kind == _executor.LUA_TUSERDATA and self.is_capsule(idx):
            ptr_v = ctypes.c_void_p(lua_touserdata(self.L, -1))
            ptr_py = decapsule(ptr_v)
            return ptr_py

        else:
            raise LuaException("can't coerce %s" % self.type_name())

    @check_stack(2, 0)
    def is_capsule(self, idx):
        idx = abs_index(self.L, idx)

        if not lua_getmetatable(self.L, idx):
            return False

        lua_pushstring(self.L, "capsule")
        lua_rawget(self.L, -2)
        ret = not lua_isnil(self.L, -1)

        # metatable and value|nil is on the stack
        lua_pop(self.L, 2)

        return ret

    @check_stack(1, 0)
    def to_python(self):
        with self._bring_to_top():
            ret = self._to_python(-1)
        return ret

    @check_stack(1)
    def __call__(self, *args):
        # NOTE!!! lua does a longjmp back to the lua_pcallk call site if
        # anything goes wrong. Because of that, Python's exception handling
        # (including finally clauses!) will *not* be triggered. This can cause
        # us to leak memory, not release/reacquire the GIL and all sorts of
        # craziness. Because of this, the actual call site is in
        # _executormodule.c who can better deal with that stuff

        self._bring_to_top(False)  # lua_pcallk consumes

        if not lua_checkstack(self.L, 2+len(args)):
            raise LuaOutOfMemoryException("__call__.checkstack")

        before_top = lua_gettop(self.L)

        try:
            lua_args = map(lambda x: LuaValue.from_python(self, x), args)
        except Exception:
            # get ourselves off of the stack
            lua_pop(self.L, 1)
            raise

        for la in lua_args:
            la._bring_to_top(False)

        # allocation limiting must only be turned on while we're operating
        # inside of a pcall, or Lua's crazy longjmp thing will kick in
        enable_limit_memory(self.L)

        # lua_pcallk will pop the function all of the arguments that we added,
        # whether or not it fails
        pcall_ret = lua_pcallk(self.L,
                               len(lua_args), _executor.LUA_MULTRET,
                               0, 0, None)

        disable_limit_memory(self.L)

        if pcall_ret == _executor.LUA_OK:
            after_top = lua_gettop(self.L)

            rets = []

            for _ in xrange(1+after_top-before_top):
                rets.append(LuaValue(self.executor))

            rets.reverse()

            return rets

        elif pcall_ret == _executor.LUA_ERRRUN:
            raise LuaStateException(self.executor)

        elif pcall_ret == _executor.LUA_ERRMEM:
            raise LuaOutOfMemoryException("%.2fmb > %.2fmb (%dc)"
                                          % (self.executor.memory_used/1024.0/1024.0,
                                             self.executor.max_memory/1024.0/1024.0,
                                             len(self.executor.cycles)))

        else:
            raise LuaException("Unknown return value from lua_pcallk: %r"
                               % (pcall_ret,))

    @check_stack(2, 0)
    def __getitem__(self, key):
        """
        If we're a table, get the given field as LuaValue

        We follow the Lua convention of returning nil for non-present keys
        """

        key = LuaValue.from_python(self.executor, key)

        with self._bring_to_top():
            kind = lua_type(self.L, -1)

            if kind != _executor.LUA_TTABLE:
                raise TypeError("can only index tables, not %r"
                                % (self.type_name(),))

            key._bring_to_top(False)

            # now the table is at -2 and the key is at -1
            lua_rawget(self.L, -2)

            # now the table is at -2 and the value is at -1
            ret = LuaValue(self.executor)

            # now only the table remains, which _bring_to_top will clean up
            return ret

    @check_stack(3, 0)
    def __setitem__(self, key, value):
        key = LuaValue.from_python(self.executor, key)
        value = LuaValue.from_python(self.executor, value)

        with self._bring_to_top():
            kind = lua_type(self.L, -1)
            if kind != _executor.LUA_TTABLE:
                raise TypeError("can only index tables, not %r"
                                % (self.type_name(),))

            key._bring_to_top(False)
            value._bring_to_top(False)

            # consumes the key and the value, leaving the table
            lua_rawset(self.L, -3)

    @check_stack(1, 0)
    def is_nil(self):
        with self._bring_to_top():
            kind = lua_type(self.L, -1)
            return kind == _executor.LUA_TNIL

    def __repr__(self):
        return "<%s (%s) %s:%d>" % (self.__class__.__name__,
                                    self.type_name(),
                                    self.executor.name,
                                    self.key)

    @classmethod
    def from_python(cls, executor, val, recursion=0, max_recursion=10):
        # weird argument rearranging to make @check_stack and copy paste
        # easier
        return cls._from_python(executor, cls, val,
                                recursion=recursion,
                                max_recursion=max_recursion)

    @staticmethod
    @check_stack(3, 0)
    def _from_python(self, cls, val, recursion=0, max_recursion=10):
        if recursion > max_recursion:
            raise ValueError("recursed too much (%d>%d)"
                             % (recursion, max_recursion))

        executor = self

        if isinstance(val, LuaValue):
            # it's already a Lua value
            return val

        elif val is None:
            lua_pushnil(self.L)
            return LuaValue(executor)

        elif isinstance(val, bool):
            lua_pushboolean(self.L, 1 if val else 0)
            return LuaValue(executor)

        elif isinstance(val, (int, long, float)):
            lua_pushnumber(self.L, lua_number_type(val))
            return LuaValue(executor)

        elif isinstance(val, str):
            lua_pushlstring(self.L, val, ctypes.c_size_t(len(val)))
            return LuaValue(executor)

        elif isinstance(val, unicode):
            as_str = val.encode('utf8')
            return cls.from_python(executor, as_str,
                                   recursion=recursion+1,
                                   max_recursion=max_recursion)

        elif isinstance(val, set):
            # sets are just tables with True as the value
            as_dict = {k: True for k in val}
            return cls.from_python(executor, as_dict,
                                   recursion=recursion+1,
                                   max_recursion=max_recursion)

        elif isinstance(val, dict):
            lua_createtable(self.L, 0, len(val))

            for k, v in val.iteritems():
                # this works but it creates a lot of overhead because it makes
                # a lot of round trips through the registry table. we can
                # probably make this faster by making this function more
                # directly recursive

                try:
                    key = cls.from_python(executor, k,
                                          recursion=recursion+1,
                                          max_recursion=max_recursion)
                    value = cls.from_python(executor, v,
                                            recursion=recursion+1,
                                            max_recursion=max_recursion)
                except Exception:
                    # if either of those fail, we need to get the table off of
                    # the stack too before we raise up
                    lua_pop(self.L, 1)
                    raise

                key._bring_to_top(False)
                value._bring_to_top(False)
                # -3 because value is -1, key is -2, so the table must be -3
                lua_rawset(self.L, -3)

                # that consumes the key and value, leaving the table

            # now the table should be at the top
            return LuaValue(executor)

        elif isinstance(val, (list, tuple)):
            # this is mostly identical to dict

            lua_createtable(self.L, len(val), 0)

            for i, value in enumerate(val, 1):
                # like for dict this could definitely be faster

                try:
                    lvalue = cls.from_python(executor, value,
                                             recursion=recursion+1,
                                             max_recursion=max_recursion)
                except Exception:
                    # if that fails, we need to get the integer and the table
                    # off of the stack before we raise up
                    lua_pop(self.L, 1)
                    raise

                lvalue._bring_to_top(False)
                lua_rawseti(self.L, -2, i)

            # now the table should be at the top
            return LuaValue(executor)

        elif callable(val) or isinstance(val, Capsule):
            val = val.inner if isinstance(val, Capsule) else val
            val_id = id(val)

            # fiddling with pointers is easier in C (leaves the userdata on
            # the stack)
            store_python_capsule(self.L,
                                 ctypes.py_object(val),
                                 ctypes.c_long(val_id))

            # assign the metatable of the userdata
            lua_getfield(self.L, _executor.LUA_REGISTRYINDEX, EXECUTOR_LUA_CAPSULE_KEY)
            lua_setmetatable(self.L, -2)

            # consume the userdata with the metatable set
            return LuaValue(executor,
                            cycle_id=(val_id, val))

        raise TypeError("Can't serialise %r. Do you need a capsule?" % (val,))


def _callable_wrapper(executor, val):
    # we get called with a new stack so anything on it belongs to us
    nargs = lua_gettop(executor.L)

    args = []

    for _ in xrange(1, nargs):
        args.append(LuaValue(executor).to_python())

    args.reverse()

    ret = val(*args)

    as_lua = LuaValue.from_python(executor, ret)

    # leave this on top of the stack for
    # call_python_function_from_lua to do the rest
    as_lua._bring_to_top(False)


def _indexable_wrapper(executor, indexable, index_idx):
    index_lua = LuaValue(executor)
    index_python = index_lua.to_python()

    try:
        found_python = indexable[index_python]
    except KeyError:
        # lua uses nil for KeyError
        return lua_pushnil(executor.L)

    # if it's a dict, continue the laziness
    if isinstance(found_python, dict):
        lv = LuaValue.from_python(executor, Capsule(found_python))
        return lv._bring_to_top(False)

    # otherwise try to serialise the value the normal way
    lv = LuaValue.from_python(executor, found_python)
    return lv._bring_to_top(False)


class Capsule(object):
    """
    A container for passing Python objects through Lua unmolested
    """

    __slots__ = ['inner']

    def __init__(self, inner):
        self.inner = inner


class LuaException(Exception):
    pass


class LuaInvariantException(LuaException):
    pass


class LuaOutOfMemoryException(LuaException):
    pass


class LuaStateException(LuaException):
    def __init__(self, executor):
        # there's currently an error on the top of the Lua stack so extract
        # it, turn it into a Python exception, and raise it. Here we don't
        # return the LuaValue version because it's very possible that we're
        # throwing away the L now and don't need to hold on it anymore
        err_string_len = ctypes.c_size_t(0)
        err_string = lua_tolstring(executor.L, -1, ctypes.byref(err_string_len))

        # since that's a ptr into Lua state we need to copy it out
        err_string_str = ctypes.string_at(err_string, err_string_len.value)

        # and consume it
        lua_pop(executor.L, 1)

        return super(LuaStateException, self).__init__(err_string_str)


class LuaSyntaxError(LuaStateException):
    pass


# make this available to importers
SANDBOXER = datafile("lua_utils/safe_sandbox.lua")


class SandboxedExecutor(object):
    def __init__(self,
                 name=None,
                 sandboxer=SANDBOXER,
                 libs=(),
                 env=None,
                 **kw):
        # bring up the VM
        self.ex = Lua(name=name, **kw)

        loaded_sandboxer = self.ex.load(
            sandboxer,
            desc='%s.sandboxer' % self.ex.name)

        self.sandbox = loaded_sandboxer()[0]

        # now that the env is built, build the libs in that env too
        for i, lib in enumerate(libs, 1):
            loaded_lib = self.sandboxed_load(
                lib,
                desc = "%s.libs[%d]" % (self.name, i))
            loaded_lib()

        # any additional envs they want available
        if env:
            for k, v in env.items():
                self.sandbox[k] = v

    def __getattr__(self, attr):
        return getattr(self.ex, attr)

    def __getitem__(self, *a, **kw):
        return self.ex.__getitem__(*a, **kw)

    def __setitem__(self, *a, **kw):
        return self.ex.__setitem__(*a, **kw)

    @check_stack(2, 0)
    def sandboxed_load(self, *a, **kw):
        loaded = self.ex.load(*a, **kw)

        with loaded._bring_to_top():
            self.sandbox._bring_to_top(False)

            if _executor.LUA_VERSION_NUM == 501:
                ret = lua_setfenv(self.L, -2)
                if ret != 1:
                    raise LuaException("couldn't setfenv?")

            else:
                # this seems really magical but it it replaces the _ENV
                # variable for this chunk (then pops the value)
                ret = lua_setupvalue(self.L, -2, 1)
                if ret is None:
                    raise LuaException("couldn't set upvalue?")

        return loaded
