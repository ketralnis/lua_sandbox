from ctypes.util import find_library
from functools import partial
from functools import wraps
import contextlib
import ctypes
import logging

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
lua_pushvalue = lua_lib.lua_pushvalue
lua_pushvalue.restype = None
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
# lua_tolstring returns a char*, but if we tell ctypes that then it will want
# to magically convert those to str for us. Unfortunately when it does that it
# doesn't account for NULL bytes which we do want to handle. so we'll pretend
# it returns a void* and handle the string conversion ourselves
lua_tolstring = lua_lib.lua_tolstring
lua_tolstring.restype = ctypes.c_void_p
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
lua_string_to_python_buffer = executor_lib.lua_string_to_python_buffer
lua_string_to_python_buffer.restype = ctypes.py_object

# function types
lua_CFunction = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)


# this is normally a macro from lua.h
def lua_pop(L, n):
    return lua_settop(L, -(n)-1)


def luaL_getmetatable(L, n):
    return lua_getfield(L, _executor.LUA_REGISTRYINDEX, n)


def lua_isnil(L, idx):
    return lua_type(L, idx) == _executor.LUA_TNIL


def luaL_typename(L, idx):
    return lua_typename(L, lua_type(L, idx))


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

            before_top = lua_gettop(self.L)

            try:
                ret = fn(self, *a, **kw)
                after_top = lua_gettop(self.L)
                if after_top-before_top != expected:
                    # the no-exception case
                    raise LuaInvariantException(
                        "check_stack in %r (needs=%d/expected=%d/before_top=%d,after_top=%d) -> %r"
                        % (fn, needs, expected, before_top, after_top, ret))
                return ret
            except LuaOutOfMemoryException:
                # if we ran out of ram the whole instance is probably blown so
                # the stack probably can't be cleaned up
                raise
            except Exception as ex:
                # on any other exception we should leave the stack unchanged so
                # double check that
                after_top = lua_gettop(self.L)
                if after_top != before_top:
                    # this will override any real exception being thrown, but
                    # it indicates a bug in the library so fixing that is
                    # probably more important than returning values from a VM
                    # that we're now unsure about the state of
                    logging.exception("double failure in lua_sandbox")
                    raise LuaInvariantException(
                        "check_stack in %r on %r (needs=%d/expected=%d/before_top=%d,after_top=%d)"
                        % (fn, ex, needs, expected, before_top, after_top))
                # otherwise it's just a regular exception so reraise whatever
                # it was
                raise
        return wrapped
    return wrap


# the libraries require about 100k to run on their own. The default of
# 2mb gives them that plus some breathing room. 0 to disable
MAX_MEMORY_DEFAULT = 2*1024*1024

MAX_RUNTIME_DEFAULT = 2.0 # (in seconds)
MAX_RUNTIME_HZ_DEFAULT = 500*1000 # how often to check (in "lua instructions")

class Lua(object):
    __slots__ = ['L', 'max_memory', 'cleanup_cache', 'name', 'references']

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
        self.references = {}

        self.L = luaL_newstate()
        self.L = ctypes.c_void_p(self.L)  # save us casts later

        if not install_control_block(self.L, ctypes.c_size_t(max_memory),
                                     ctypes.py_object(self.references)):
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

        # we need to be able to dealloc them. even though references is
        # available in the control block, free_python_capsule may run *after*
        # the control block isn't available anymore so it'll need his own copy
        lua_pushlightuserdata(self.L, ctypes.py_object(self.references))
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

        # so we can identify it
        lua_pushstring(self.L, "capsule")
        lua_setfield(self.L, -2, "capsule")

        lua_pop(self.L, 1)  # get the metatable off the stack

    def gc(self):
        "Force a garbage collection"
        lua_gc(self.L, _executor.LUA_GCCOLLECT, 0)

    @check_stack(2, 0)
    def registry(self, key):
        sv = StackValue.from_python(self, key)
        # stack is [sv]
        lua_rawget(self.L, _executor.LUA_REGISTRYINDEX)
        # consumes sv leaving [registry]
        return RegistryValue(self) # consumes [registry]

    @check_stack(1, 0)
    def set_global(self, key, value):
        if not isinstance(key, str):
            raise TypeError("key must be str, not %r" % (key,))

        sv = StackValue.from_python(self, value)
        # stack is [value]
        lua_setglobal(self.L, key) # consumes value leaving []

    def __setitem__(self, key, value):
        return self.set_global(key, value)

    def __delitem__(self, key):
        self[key] = None

    @check_stack(1, 0)
    def get_global(self, key):
        if not isinstance(key, str):
            raise TypeError("key must be str, not %r" % (key,))

        lua_getglobal(self.L, key)
        # stack is [value]
        return RegistryValue(self) # consumes [value]

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
        # leaves [code] on the stack if successful

        if load_ret == _executor.LUA_OK:
            # stack is [code]
            return RegistryValue(self)  # consumes code
        elif load_ret == _executor.LUA_ERRSYNTAX:
            raise LuaSyntaxError(self)
        elif load_ret == _executor.LUA_ERRMEM:
            raise LuaOutOfMemoryException('load')
        else:
            raise LuaStateException(self)

    @check_stack(1, 0)
    def create_table(self):
        lua_createtable(self.L, 0, 0)
        return RegistryValue(self)

    def __del__(self):
        if self.L:
            self.cleanup_cache['wrapped_lua_close'](self.L)


class _LuaValue(object):
    pass


class StackValue(_LuaValue):
    """
    Represents a Lua value on the Lua stack.

    Because this is really just a wrapper around the integer representing the
    index on the stack, it requires manual stack management! It doesn't push or
    pop itself unless you ask it to. You probably shouldn't use this if it can
    escape a single function or so, use RegistryValue for that.

    Internally we should be using mostly StackValue, but references we leak to
    the outside should be mostly RegistryValue
    """
    def __init__(self, executor, idx=None):
        """
        Build a StackValue off of whatever's on top of the stack and leave it
        there, keeping track of the index
        """
        self.executor = executor
        self.L = executor.L
        self.idx = abs_index(self.L, idx) if idx is not None else lua_gettop(self.L)

    def __repr__(self):
        return "<%s (%s) %s:%d>" % (self.__class__.__name__,
                                    self.lua_type_name(),
                                    self.executor.name,
                                    self.idx)

    @check_stack(0, -1)
    def pop(self):
        if self.idx is None:
            raise LuaInvariantException("can't pop myself twice")
        if lua_gettop(self.L) != self.idx:
            # this is a very basic check that doesn't catch even basic failures
            raise LuaInvariantException("popping someone else!")
        self.idx = None
        lua_pop(self.L, 1)

    @check_stack()
    def lua_type(self):
        return lua_type(self.L, self.idx)

    def lua_type_name(self):
        # extract the value
        name = lua_typename(self.L, self.lua_type())
        return name

    @contextlib.contextmanager
    def _bring_to_top(self, cleanup_after=True):
        "Get the value to the top of the stack"
        if not lua_checkstack(self.L, 1):
            raise LuaOutOfMemoryException("_bring_to_top.checkstack")

        # it's already on the stack somewhere, this just duplicates it to the
        # top position
        lua_pushvalue(self.L, self.idx)

        if cleanup_after:
            return self.__bring_to_top_cleanup()

    @contextlib.contextmanager
    def __bring_to_top_cleanup(self):
        try:
            new_sv = StackValue(self.executor, -1)
            yield new_sv
        finally:
            lua_pop(self.L, 1)

    @check_stack(1, 0)
    def as_ref(self):
        """
        Return a RegistryValue for this stack entry, leaving it on the stack
        """
        self._bring_to_top(False)
        return RegistryValue(self.executor)

    @check_stack(1, -1)
    def to_ref(self):
        """
        Return a RegistryValue for this stack entry, consuming it from the stack
        """
        self._bring_to_top(False)
        ret = RegistryValue(self.executor)
        self.pop() # consume ourselves
        return ret

    @check_stack(1, 0)
    def to_python(self):
        return self._to_python(self.idx)

    def _to_python(self, idx):
        kind = lua_type(self.L, idx)  # this idx may not be referring to us

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

            # stack is [nil]

            while True:
                nidx = lua_next(self.L, idx)

                if nidx == 0:
                    break

                # stack is [key, value]

                # `key' is at index -2 and `value' at index -1
                try:
                    value = self._to_python(-1)
                    key = self._to_python(-2)
                except Exception:
                    lua_pop(self.L, 2)
                    raise

                ret[key] = value

                # removes [value], leaving [key] for next iteration
                lua_pop(self.L, 1)

            return ret

        elif kind == _executor.LUA_TFUNCTION:
            # it's already callable so we just need to return a RegistryValue
            # that represents it. We use a RegistryValue instead of a
            # StackValue because our caller is expecting to get a Python object
            # that they can just pass around without doing any stack management
            with StackValue(self.executor, idx=idx)._bring_to_top() as sv:
                ret = sv.as_ref()
            return ret

        ###### LEFT OFF HERE: problem is is_capsule isn't being threaded around?
        elif kind == _executor.LUA_TUSERDATA and self.is_capsule(idx):
            ptr_v = ctypes.c_void_p(lua_touserdata(self.L, idx))
            ptr_py = decapsule(ptr_v)
            return ptr_py

        else:
            raise LuaException("can't coerce %s" % self.lua_type_name())

    @check_stack(2, 0)
    def is_capsule(self, idx=None):
        if idx is None:
            idx = self.idx
        idx = abs_index(self.L, idx)
        if not lua_getmetatable(self.L, idx):
            return False
        # stack is [metatable]
        lua_pushstring(self.L, "capsule")
        # stack is [metatable, magic string]
        lua_rawget(self.L, -2)  # pops [magic string], adds [value]
        # stack is [metatable, value|nil]
        ret = not lua_isnil(self.L, -1)
        # stack is [metatable, value|nil] so clear them off
        lua_pop(self.L, 2)
        return ret

    @check_stack(1)
    def __call__(self, *args):
        # NOTE!!! lua does a longjmp back to the lua_pcallk call site if
        # anything goes wrong. Because of that, Python's exception handling
        # (including finally clauses!) will *not* be triggered. This can cause
        # us to leak memory, not release/reacquire the GIL and all sorts of
        # craziness. Because of this, the actual call site is in
        # _executormodule.c who can better deal with that stuff

        # print 'bt-2', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

        self._bring_to_top(False)  # lua_pcallk consumes

        # smking gun? it's not on top after this?
        # print 'bt-1', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

        if not lua_checkstack(self.L, 2+len(args)):
            raise LuaOutOfMemoryException("__call__.checkstack")

        before_top = lua_gettop(self.L)
        # print 'bt', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

        lua_args = []

        # get all of the args on the stack and ready
        for arg in args:
            try:
                sv = StackValue.from_python(self.executor, arg)
            except Exception:
                # get ourself and the args so far off of the stack
                lua_pop(self.l, 1+len(lua_args))
                raise
            else:
                lua_args.append(sv)

        # print 'bt2', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

        # allocation limiting must only be turned on while we're operating
        # inside of a pcall, or Lua's crazy longjmp thing will kick in
        enable_limit_memory(self.L)

        # lua_pcallk will pop the function and all of the arguments that we
        # added, whether or not it fails
        pcall_ret = lua_pcallk(self.L,
                               len(lua_args), _executor.LUA_MULTRET,
                               0, 0, None)

        # print 'at0', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

        disable_limit_memory(self.L)

        if pcall_ret == _executor.LUA_OK:
            after_top = lua_gettop(self.L)
            # print 'at1', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

            rets = []

            # consumes them from the stack as we go
            for _ in xrange(1+after_top-before_top):
                # this puts them into RegistryValues but they could just as
                # easy be StackValues to be translated in
                # RegistryValue.__call__ like we do other things
                rets.append(RegistryValue(self.executor))

            # print 'at2', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

            rets.reverse()

            return rets

        elif pcall_ret == _executor.LUA_ERRRUN:
            raise LuaStateException(self.executor)

        elif pcall_ret == _executor.LUA_ERRMEM:
            raise LuaOutOfMemoryException("%.2fmb > %.2fmb (%dc)"
                                          % (self.executor.memory_used/1024.0/1024.0,
                                             self.executor.max_memory/1024.0/1024.0,
                                             len(self.executor.references)))

        else:
            raise LuaException("Unknown return value from lua_pcallk: %r"
                               % (pcall_ret,))


    @contextlib.contextmanager
    def as_buffer(self):
        """
        Yields a buffer() directly into the memory that contains this Lua string.

        This is useful for zero-allocation access to lua strings, but comes at
        the cost of needing to be very careful with how you affect the Lua VM
        while you hold onto this reference. The referred string must remain on
        the Lua stack the whole time you retain a reference to it, you must
        release that reference before as_buffer() returns, and you must never
        write to the buffer
        """
        if self.lua_type() != _executor.LUA_TSTRING:
            raise TypeError("as_buffer only works on strings, not %s!"
                            % (self.lua_type_name(),))

        size = ctypes.c_size_t(0)
        ptr = lua_tolstring(self.L, self.idx, ctypes.byref(size))
        # size-1 because Lua includes room for a null byte but we don't
        # need it
        cls = (ctypes.c_char * size.value)
        buff = cls.from_address(ptr)
        yield buff

    @check_stack(2, 0)
    def getitem(self, key):
        """
        If we're a table, get the given field as RegistryValue

        We follow the Lua convention of returning nil for non-present keys
        """
        if self.lua_type() != _executor.LUA_TTABLE:
            raise TypeError("can only index tables, not %r"
                            % (self.lua_type_name(),))

        key = StackValue.from_python(self.executor, key)
        # stack is [key]
        lua_rawget(self.L, self.idx)  # consumes [key] leaving [value]
        ret = RegistryValue(self.executor)   # consumes [value] leaving []
        return ret

    @check_stack(2, 0)
    def setitem(self, key, value):
        if self.lua_type() != _executor.LUA_TTABLE:
            raise TypeError("can only index tables, not %r"
                            % (self.lua_type_name(),))

        # if we throw an exception, how many things need popping
        err_stack = 0

        try:
            key = StackValue.from_python(self.executor, key)
            err_stack += 1
            value = StackValue.from_python(self.executor, value)
            err_stack += 1
        except Exception as e:
            lua_pop(self.L, err_stack)
            raise

        # stack is [key, value]
        lua_rawset(self.L, self.idx) # consumes both leaving []

    @check_stack(1, 0)
    def is_nil(self):
        return self.lua_type() == _executor.LUA_TNIL

    @classmethod
    def from_python(cls, executor, val, recursion=0, max_recursion=10):
        """
        Construct a StackValue out of a Python object, recursively, including
        dealing with Capsules/callables. Leaves it as a new value on the stack
        """
        # weird argument rearranging to make @check_stack and copy paste
        # easier
        return cls._from_python(executor, cls, val,
                                recursion=recursion,
                                max_recursion=max_recursion)

    @staticmethod
    @check_stack(3, 1)
    def _from_python(self, cls, val, recursion=0, max_recursion=10):
        if recursion > max_recursion:
            raise ValueError("recursed too much (%d>%d)"
                             % (recursion, max_recursion))

        executor = self

        if isinstance(val, _LuaValue):
            # it's already a Lua value
            sv = val._bring_to_top(False)
            return sv

        elif val is None:
            lua_pushnil(self.L)
            return StackValue(executor)

        elif isinstance(val, bool):
            lua_pushboolean(self.L, 1 if val else 0)
            return StackValue(executor)

        elif isinstance(val, (int, long, float)):
            lua_pushnumber(self.L, lua_number_type(val))
            return StackValue(executor)

        elif isinstance(val, str):
            lua_pushlstring(self.L, val, ctypes.c_size_t(len(val)))
            return StackValue(executor)

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

            # stack is [table]

            for k, v in val.iteritems():
                # if an exception is thrown, how many things need popping from
                # the stack (starting with just [table])
                err_stack = 1

                try:
                    key = cls.from_python(executor, k,
                                          recursion=recursion+1,
                                          max_recursion=max_recursion)
                    err_stack += 1
                    value = cls.from_python(executor, v,
                                            recursion=recursion+1,
                                            max_recursion=max_recursion)
                    err_stack += 1
                except Exception:
                    # if either of those fail, we need to get the table off of
                    # the stack too before we raise up
                    lua_pop(self.L, err_stack)
                    raise

                # stack is now [table, key, value]

                lua_rawset(self.L, -3) # -3 is the table
                # that consumes [key, value], leaving [table]

            # now the table should be at the top
            return StackValue(executor)

        elif isinstance(val, (list, tuple)):
            # this is mostly identical to dict

            lua_createtable(self.L, len(val), 0)

            for i, value in enumerate(val, 1):
                # if an exception is thrown, how many things need popping from
                # the stack (starting with just [table])
                err_stack = 1

                try:
                    lvalue = cls.from_python(executor, value,
                                             recursion=recursion+1,
                                             max_recursion=max_recursion)
                    err_stack += 1
                except Exception:
                    lua_pop(self.L, err_stack)
                    raise

                # stack is [table, value]
                lua_rawseti(self.L, -2, i) # consumes [value]
                # stack is [table]

            # now the table should be at the top
            return StackValue(executor)

        elif callable(val) or isinstance(val, Capsule):
            lval = val.inner if isinstance(val, Capsule) else val

            should_cache = recursive = raw_lua_args = 0

            if isinstance(val, Capsule):
                if val.cache:
                    should_cache = 1
                if val.recursive:
                    recursive = 1
                if val.raw_lua_args:
                    raw_lua_args = 1

            # fiddling with pointers is easier in C (leaves the userdata on
            # the stack)
            store_python_capsule(self.L,
                                 ctypes.py_object(lval),
                                 should_cache,
                                 recursive,
                                 raw_lua_args)

            sv = StackValue(executor)
            # stack is [userdata]

            # leave the userdata on the stack with the metatable set
            return StackValue(executor)

        raise TypeError("Can't serialise %r. Do you need a capsule?" % (val,))


class RegistryValue(_LuaValue):
    """
    Represents a Lua value stored on the Heap

    This works by using the Lua registry and keeping refs into it. It's safe
    for these values to escape and be kept in Python memory but there's some
    overhead to getting stuff into and out of the registry all of the time
    """

    def __init__(self, executor):
        """
        Build a RegistryValue off of whatever's on the top of the stack, then
        pop it off. We store the underlying Lua value in Lua's global registry
        and we keep track of the ref
        """
        assert isinstance(executor, Lua), "expected Lua, got %r" % (executor,)

        # it's important that we keep a reference to the executor because he's
        # the one that keeps the lua_State* live
        self.executor = executor
        self.L = executor.L
        self._create()

    @check_stack(2, -1)
    def _create(self):
        # right now the top of the stack contains the item that we're storing

        # pops it off and stores the integer reference in the registry
        self.key = luaL_ref(self.L, _executor.LUA_REGISTRYINDEX)

        # now we've consumed it from the stack so it's empty for our purposes

        # this is a little weird, but we need to hold on to these so that our
        # __del__ has access to them
        self.cleanup_cache = dict(
            luaL_unref=luaL_unref,
            LUA_REGISTRYINDEX=_executor.LUA_REGISTRYINDEX,
        )

    def __del__(self):
        # this takes us out of the registry. if we refer to a Python object,
        # freeing that may trigger the __gc metamethod which will eventually
        # tell Python to free it as well
        self.cleanup_cache['luaL_unref'](self.L,
            self.cleanup_cache['LUA_REGISTRYINDEX'],
            self.key)

    def __repr__(self):
        return "<%s (%s) %s:%d>" % (self.__class__.__name__,
                                    self.lua_type_name(),
                                    self.executor.name,
                                    self.key)

    def as_ref(self):
        return self

    # from here on out we're duplicating/translating StackValue methods

    def _bring_to_top(self, cleanup_after=True):
        "Get the value to the top of the stack"
        if not lua_checkstack(self.L, 1):
            raise LuaOutOfMemoryException("_bring_to_top.checkstack")

        # get the value to the top of the stack
        # print 'X1', self.key, lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)
        lua_rawgeti(self.L, _executor.LUA_REGISTRYINDEX, self.key)
        # print 'X2', lua_gettop(self.L), lua_gettop(self.L) and luaL_typename(self.L, -1)

        if cleanup_after:
            return self.__bring_to_top_cleanup()

    @contextlib.contextmanager
    def __bring_to_top_cleanup(self):
        try:
            new_sv = StackValue(self.executor, -1)
            yield new_sv
        finally:
            lua_pop(self.L, 1)

    def _stack_value(fn_name):
        sv_descriptor = getattr(StackValue, fn_name)

        @wraps(sv_descriptor)
        def _with_stack_value(self, *a, **kw):
            with self._bring_to_top() as sv:
                return sv_descriptor(sv, *a, **kw)
        return _with_stack_value

    lua_type = _stack_value('lua_type')
    lua_type_name = _stack_value('lua_type_name')
    is_nil = _stack_value('is_nil')
    is_capsule = _stack_value('is_capsule')
    to_python = _stack_value('to_python')

    @contextlib.contextmanager
    def as_buffer(self):
        with self._bring_to_top() as sv:
            with sv.as_buffer() as buff:
                yield buff

    @check_stack(1, 0)
    def __setitem__(self, key, value):
        with self._bring_to_top() as sv:
            sv.setitem(key, value)

    @check_stack(1, 0)
    def __getitem__(self, key):
        with self._bring_to_top() as sv:
            return sv.getitem(key).as_ref()

    @check_stack(1, 0)
    def __call__(self, *args):
        # print 1, lua_gettop(self.L)
        with self._bring_to_top() as sv:
            # print 2, lua_gettop(self.L)
            # he returns RegistryValues for our convenience here
            ret = sv(*args)
            # print 3, lua_gettop(self.L)
        # print 4, lua_gettop(self.L)
        return ret

    @classmethod
    def from_python(cls, executor, val):
        return StackValue.from_python(executor, val).to_ref()


def _callable_wrapper(executor, val, raw_lua_args=False):
    # we get called with a new stack so anything on it belongs to us
    nargs = lua_gettop(executor.L)

    args = []

    # eat the arguments off of the stack
    for _ in xrange(1, nargs):
        if raw_lua_args:
            # we pass him RegistryValues because we don't know if he's going to
            # keep any of his own references, for instance if he memoises
            # himself
            args.append(RegistryValue(executor))
        else:
            sv = StackValue(executor)
            as_py = sv.to_python()
            sv.pop()
            args.append(as_py)

    args.reverse()

    ret = val(*args)

    # leave this on top of the stack for call_python_function_from_lua to do
    # the rest
    as_lua = StackValue.from_python(executor, ret)


def _indexable_wrapper(executor, indexable, should_cache, recursive):
    index_lua = StackValue(executor)
    index_python = index_lua.to_python()
    index_lua.pop()

    try:
        found_python = indexable[index_python]
    except KeyError:
        # lua uses nil for KeyError
        return lua_pushnil(executor.L)

    # if it's a dict, continue the laziness
    if recursive and isinstance(found_python, dict):
        capsule = Capsule(found_python,
                          cache=should_cache,
                          recursive=True)
        StackValue.from_python(executor,
                               capsule)
        # stack is just [capsule]
        # leave that on the stack to be returned
        return

    # otherwise try to serialise the value the normal way
    StackValue.from_python(executor, found_python)
    # stack is [value]
    # leave that on the stack to be returned


class Capsule(object):
    """
    A container for passing Python objects through Lua unmolested
    """

    __slots__ = ['inner', 'cache', 'recursive', 'raw_lua_args']

    def __init__(self, inner, cache=True, recursive=True, raw_lua_args=False):
        self.inner = inner
        self.cache = cache
        self.recursive = recursive
        self.raw_lua_args = raw_lua_args


class LuaException(Exception):
    def __str__(self):
        return "%s(%s)" % (self.__class__.__name__, self.message)

    def __repr__(self):
        return str(self)


class LuaInvariantException(LuaException):
    pass


class LuaOutOfMemoryException(LuaException):
    pass


class LuaStateException(LuaException):
    def __init__(self, executor):
        # there's currently an error on the top of the Lua stack so extract
        # it, turn it into a Python exception, and raise it.

        self.lua_value = lua_value = RegistryValue(executor)

        typ = lua_value.lua_type()
        # pretty-print if it's easy
        if typ in (
                _executor.LUA_TNUMBER,
                _executor.LUA_TNIL,
                _executor.LUA_TBOOLEAN,
                _executor.LUA_TSTRING):
            message = repr(lua_value.to_python())
        elif typ == _executor.LUA_TUSERDATA and lua_value.is_capsule():
            python_value = lua_value.to_python()
            message = repr(python_value)
            self.__cause__ = python_value
        else:
            message = repr(lua_value)

        return super(LuaStateException, self).__init__(message)


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
        # print '*'*20, loaded_sandboxer
        sandboxer_result = loaded_sandboxer()
        self.sandbox = sandboxer_result[0]

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
                # this seems really magical but it replaces the _ENV variable
                # for this chunk (then pops the value)
                ret = lua_setupvalue(self.L, -2, 1)
                if ret is None:
                    raise LuaException("couldn't set upvalue?")

        return loaded
