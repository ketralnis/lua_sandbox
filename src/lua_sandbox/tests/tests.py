# -*- coding: utf-8 -*-

import multiprocessing
import os
import re
import threading
import time
import unittest

from lua_sandbox.executor import LuaException
from lua_sandbox.executor import LuaInvariantException
from lua_sandbox.executor import LuaOutOfMemoryException
from lua_sandbox.executor import LuaSyntaxError
from lua_sandbox.executor import SandboxedExecutor
from lua_sandbox.executor import check_stack
from lua_sandbox.executor import _executor
from lua_sandbox.executor import Capsule


class SimpleSandboxedExecutor(object):
    def __init__(self, name=None, **kw):
        self.lua = SandboxedExecutor(name=name, **kw)

    def execute(self, program, env={}):
        loaded = self.lua.sandboxed_load(program)
        for k, v in env.items():
            self.lua.sandbox[k] = v

        return tuple(x.to_python() for x in loaded())


def skip_if_luajit(fn):
    if _executor.LUA_VERSION_NUM == 501:
        return unittest.skip("unsupported on luajit")(fn)
    return fn


def only_on_luajit(fn):
    if _executor.LUA_VERSION_NUM != 501:
        return unittest.skip("only supported on luajit")(fn)
    return fn


class TestLuaExecution(unittest.TestCase):
    def setUp(self, *a, **kw):
        self.ex = SimpleSandboxedExecutor(
            name=self.id().encode('ascii'),
            max_memory=None)

    def test_basics1(self):
        program = b"""
            return 1
        """
        self.assertEqual(self.ex.execute(program, {}),
                         (1.0,))

    def test_basics2(self):
        program = b"""
            return a, b, a+b
        """
        self.assertEqual(self.ex.execute(program, {'a': 1, 'b': 2}),
                         (1.0, 2.0, 3.0))

    def test_basics3(self):
        program = b"""
            foo = {}
            while #foo < 5 do
                foo[#foo+1] = #foo+1
            end
            return foo
        """
        self.assertEqual(self.ex.execute(program, {}),
                         ({1.0: 1.0,
                           2.0: 2.0,
                           3.0: 3.0,
                           4.0: 4.0,
                           5.0: 5.0},))

    def test_check_stack(self):
        # we rely on the @check_stack decorator a lot to detect stack leaks, so
        # make sure it at least works
        def _fn(executor):
            executor.create_table()._bring_to_top(False)

        with self.assertRaises(LuaException):
            check_stack(0, 0)(_fn)(self.ex.lua)

    def test_parse_error(self):
        program = b"()code"
        with self.assertRaises(LuaSyntaxError):
            self.ex.lua.load(program)

    def test_serialize_deserialize(self):
        program = b"""
            return foo
        b"""
        input_data = {
            1: 2,
            2: 3,
            4: "abc",
            5: ('a', 'b', 'c'),
            5.5: ['a', 'b', 'c'],
            6: 6.5,
            7: None,
            8: '',
            9: True,
            10: False,
            11: {'a': {1: 2}},
            12: u"héllo",
            13: b"hello",
        }
        expected_output = {
            1.0: 2.0,
            2.0: 3.0,
            4.0: b"abc",
            5.0: {1.0: 'a', 2.0: 'b', 3.0: 'c'},
            5.5: {1.0: 'a', 2.0: 'b', 3.0: 'c'},
            6.0: 6.5,
            # 7 disappears
            8.0: '',
            9.0: True,
            10.0: False,
            11.0: {'a': {1.0: 2.0}},
            12: u"héllo".encode('utf8'),
            13: b"hello",
            }

        self.assertEqual(self.ex.execute(program,
                                   {'foo': input_data}),
                         (expected_output,))

    def test_serialize_unicode(self):
        program = b"""
            return data
        """

        english = 'hello'
        chinese = u'你好'

        input_data = {english: chinese}

        self.assertEqual(self.ex.execute(program,
                                   {'data': input_data}),
                         ({english.encode('ascii'): chinese.encode('utf8')},))

    def test_no_weird_python_types(self):
        program = b"""
            return foo
        """

        with self.assertRaises(TypeError):
            self.ex.execute(program, {'foo': object()})

        with self.assertRaises(ValueError):
            # recursive structure
            d = {}
            d['foo'] = d
            self.ex.execute(program, {'foo': d})

    def test_capsule_return(self):
        program = b"""
            return capsule
        """

        obj = object()
        capsule = Capsule(obj)
        ret = self.ex.execute(program, {'capsule': capsule})
        self.assertIs(obj, ret[0])

    def test_capsule_caches(self):
        program = b"""
            first_time = capsule.property
            update_value()
            second_time = capsule.property
            return first_time, second_time
        """

        d = {'property': 'foo'}
        capsule = Capsule(d, cache=True)
        def update_value():
            d['property'] = 'bar'

        ret = self.ex.execute(program, {'capsule': capsule,
                                        'update_value': update_value})
        self.assertEquals(ret, ('foo', 'foo'))

    def test_capsule_no_caches(self):
        program = b"""
            first_time = capsule.property
            update_value()
            second_time = capsule.property
            return first_time, second_time
        """

        d = {'property': 'foo'}
        capsule = Capsule(d, cache=False)
        def update_value():
            d['property'] = 'bar'

        ret = self.ex.execute(program, {'capsule': capsule,
                                        'update_value': update_value})
        self.assertEquals(ret, ('foo', 'bar'))

    def test_capsule_return_pass_arg(self):
        success = []

        orig = object()
        capsule = Capsule(orig)

        def _fn(cap):
            success.append(cap)
            return Capsule(cap)

        program = b"""
            return foo(capsule)
        """

        ret = self.ex.execute(program, {'foo': _fn, 'capsule': capsule})

        self.assertEqual(success, [orig])
        self.assertEqual(ret, (orig,))

    def test_capsule_index(self):
        data = {'foo': 5, 'bar': {'baz': 10}, 'str1': 'str2'}

        program = b"""
            return data.foo, data.bar.baz, data.str1, data.notthere
        """

        ret = self.ex.execute(program, {'data': Capsule(data)})
        self.assertEqual(ret, (5.0, 10.0, 'str2', None))

    def test_capsule_none(self):
        program = b"return data"
        ret = self.ex.execute(program, {'data': Capsule(None)})
        self.assertEqual(ret, (None,))

    def test_capsule_lazy(self):
        loaded = self.ex.lua.sandboxed_load(b"""
            return data.more_data
        """)

        self.ex.lua.sandbox['data'] = Capsule(dict(
            data=1,
            more_data="string",
            # if the capsule isn't lazy, this will keep it from being
            # passed in
            something_non_serialiseable=object(),
        ))

        result = [x.to_python() for x in loaded()]
        self.assertEqual(result, ["string",])

    def test_function_noargs(self):
        program = b"""
            return foo()
        """
        ret = self.ex.execute(program, {'foo': lambda: 5})

        self.assertEqual((5.0,), ret)

    def test_function_passing(self):
        closed = []
        def closure(argument1, argument2):
            closed.append(argument1)
            closed.append(argument2)
            return argument1 ** argument2, argument2 ** argument1

        program = b"""
            a = 1+3
            return foo(2, 3)
        """
        ret = self.ex.execute(program, {'foo': closure})

        self.assertEqual(({1.0: 8.0, 2.0: 9.0},), ret)
        self.assertEqual([2.0, 3.0], closed)

    def test_function_args(self):
        program = b"""
        local function multiplier(a, b)
            return a*b
        end
        return multiplier
        """
        loaded = self.ex.lua.load(program)
        func = loaded()[0]
        multiplied = func(3, 7)
        self.assertEqual([21.0], [x.to_python() for x in multiplied])

    def test_method_passing(self):
        class MyObject(object):
            def double(self, x):
                return x*2

        program = b"""
            return doubler(4)
        """
        ret = self.ex.execute(program, {'doubler': MyObject().double})
        self.assertEqual((8.0,), ret)

    def test_regular_exception(self):
        program = b"""
            error("I'm an error!")
        """
        try:
            self.ex.execute(program)
        except LuaException as e:
            self.assertEqual(str(e), 'LuaStateException(b\'[string "Lua"]:2: I\\\'m an error!\')')
        else:
            self.assertTrue(False)

    def test_number_exception(self):
        program = b"""
            error(3.14159)
        """
        try:
            self.ex.execute(program)
        except LuaException as e:
            self.assertEqual(str(e), 'LuaStateException(b\'[string "Lua"]:2: 3.14159\')')
            # lua doesn't thread the original number back, it coerces to a
            # string
            self.assertEqual(e.lua_value.to_python(), b'[string "Lua"]:2: 3.14159')
        else:
            self.assertTrue(False)

    def test_table_exception(self):
        program = b"""
            error({['whole message']= 'this is my message'})
        """
        try:
            self.ex.execute(program)
        except LuaException as e:
            self.assertEqual(e.lua_value.to_python(), {
                b'whole message': b'this is my message',
            })
        else:
            self.assertTrue(False)

    def test_pyfunction_exception(self):
        program = b"""
            return foo("hello")
        """
        class MyException(Exception): pass

        def bad_closure(x):
            raise MyException("nuh uh")

        try:
            self.ex.execute(program, {'foo': bad_closure})
        except LuaException as e:
            self.assertIsInstance(e.lua_value.to_python(), MyException)
            self.assertIsInstance(e.__cause__, MyException)
        else:
            self.assertTrue(False)

    def test_pyobject_exception(self):
        obj = object()
        program = b"""
            error(foo)
        """

        try:
            self.ex.execute(program, {'foo': Capsule(obj)})
        except LuaException as e:
            self.assertIs(e.lua_value.to_python(), obj)
        else:
            self.assertTrue(False)

    def test_assertions(self):
        program = b"""
            assert(false)
        """
        with self.assertRaises(LuaException):
            self.ex.execute(program)

        program = b"""
            error("nuh uh")
        """
        with self.assertRaises(LuaException):
            self.ex.execute(program)

    def test_setitem(self):
        program = b"return x"

        # round trip
        self.ex.lua[b'x'] = 5
        self.assertEqual(self.ex.lua[b'x'].to_python(), 5.0)

        # nils
        self.assertEqual(self.ex.lua[b'bar'].type_name(), b'nil')
        self.assertEqual(self.ex.lua[b'bar'].to_python(), None)

        # loaded code can get to it
        loaded = self.ex.lua.load(program)
        returns = [x.to_python() for x in loaded()]
        self.assertEqual(returns, [5.0])

    def test_createtable(self):
        t = self.ex.lua.create_table()
        t['foo'] = 5
        self.assertEquals(t['foo'].to_python(), 5.0)
        self.assertEquals(t['bar'].type_name(), b'nil')
        self.assertEquals(t['bar'].to_python(), None)
        self.assertTrue(t['bar'].is_nil())

        with self.assertRaises(TypeError):
            x = t['foo']['bar']

        with self.assertRaises(TypeError):
            t['foo']['bar'] = 5

    def test_buffer_interface(self):
        # we can get a buffer into a Lua string and, and re.search works on
        # that buffer
        s = b""" return "this is my string" """
        loaded = self.ex.lua.load(s)
        lua_string, = loaded()
        with lua_string.as_buffer() as buff:
            self.assertEqual(re.search(b'my (string.*)', buff).groups(),
                             (b'string',))
            self.assertEqual(len(buff), len(b'this is my string'))

        # this is the regular callback way with lots of copies, here mostly for
        # demonstration
        def slow_search(pat, x):
            result = re.search(pat, x)
            if result:
                return result.groups()
        ret = self.ex.execute(b""" return re.search("(string)", str)[1] """,
                              {
                                  're': {'search': slow_search},
                                   'str': 'this is my string'
                              })
        self.assertEqual(ret, ('string',))

        # and this is a faster callback which can avoid the copy on the `x`
        # argument
        def fast_search(pat, x):
            pat = pat.to_python()
            with x.as_buffer() as buff:
                result = re.search(pat, buff)
            if result:
                # this does still copy if there is a match
                return result.groups()
        ret = self.ex.execute(b""" return re.search("(string)", str)[1] """,
                              {
                                  're': {
                                    'search': Capsule(fast_search,
                                                      raw_lua_args=True),
                                   },
                                   'str': 'this is my string'
                              })
        self.assertEqual(ret, ('string',))


class TestSafeguards(TestLuaExecution):
    def setUp(self, *a, **kw):
        self.ex = SimpleSandboxedExecutor(name=self.id().encode('ascii'),
            max_memory=5*1024*1024)

    def test_memory(self):
        def _tester(program):
            start_time = time.time()
            with self.assertRaises(LuaOutOfMemoryException):
                self.ex.execute(program)
            self.assertLess(time.time()-start_time, 1.1)

        _tester(b"""
            foo = {}
            while #foo < 500000000 do
                foo[#foo+1] = 1
            end
            return 1
        """)

    def test_memory_used(self):
        self.ex.lua[b'some_var'] = '*'*(1024*1024)
        self.assertGreater(self.ex.lua.memory_used, 1024*1024)

    def test_timeout(self):
        def _tester(program):
            start_time = time.time()
            with self.assertRaises(LuaException):
                with self.ex.lua.limit_runtime(0.5, disable_jit=True):
                    self.ex.execute(program)
            self.assertLess(time.time()-start_time, 0.7)

        _tester(b"""
            foo = {}
            while true do
            end
            return 1
        """)

        with self.ex.lua.limit_runtime(1.0, disable_jit=True):
            # make sure the limiter doesn't just always trigger
            self.ex.execute(b"return 5")

    def test_no_print(self):
        # make sure we didn't pass any libraries to the client program
        program = b"""
            print(foo)
            return 0
        """
        with self.assertRaises(LuaException):
            self.ex.execute(program, {'foo':0})

    @unittest.skip("allowing this for now")
    def test_no_patterns(self):
        # there are some lua pattern operations you can do that are super slow,
        # so we block them entirely in the SimpleSandboxingExecutor
        program = b"""
            return string.find(("a"):rep(1e4), ".-.-.-.-b$")
        """

        started = time.time()
        with self.assertRaises(LuaException):
            self.ex.execute(program)

        self.assertLess(time.time() - started, 1.0)

    def test_have_good_libs(self):
        # make sure we did pass the libraries that are okay
        program = b"""
            math.abs(-1)
            table.sort({})
            return foo
        """
        self.ex.execute(program, {'foo': 0})

    @skip_if_luajit
    def test_luajit_not_present(self):
        lua = self.ex.lua
        self.assertEqual(lua.jit_mode(), None)

    @only_on_luajit
    def test_luajit_present(self):
        lua = self.ex.lua
        self.assertNotEqual(lua.jit_mode(), None)

    @only_on_luajit
    def test_luajit_mode(self):
        lua = self.ex.lua
        lua.jit_mode().compiler_mode(True)
        lua.jit_mode().compiler_mode(False)
        lua.jit_mode().flush_compiler()


class TestReusingExecutor(TestLuaExecution):
    def __init__(self, *a, **kw):
        unittest.TestCase.__init__(self, *a, **kw)
        self.ex = SimpleSandboxedExecutor()

    def setUp(self):
        pass


if __name__ == '__main__':
    if os.environ.get('LEAKTEST', False):
        from pympler import tracker

        tr = tracker.SummaryTracker()

        def _fn():
            for _ in range(10):
                unittest.main(verbosity=0, exit=False)

        while True:
            threads = []
            for _ in range(multiprocessing.cpu_count()):
                t = threading.Thread(target=_fn)
                t.daemon = True
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            del t
            del threads

            tr.print_diff()

    else:
        unittest.main()