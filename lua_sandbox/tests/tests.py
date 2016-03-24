import unittest
import time

from lua_sandbox.executor import SimpleSandboxedExecutor


class TestLuaExecution(unittest.TestCase):
    def setUp(self):
        self.ex = SimpleSandboxedExecutor()

    def test_basics1(self):
        program = """
            return 1
        """
        self.assertEqual(self.ex._stack_top(), 0)
        self.assertEqual(self.ex.execute(program, {}),
                         (1.0,))
        self.assertEqual(self.ex._stack_top(), 0)

    def test_basics2(self):
        program = """
            return a, b, a+b
        """
        self.assertEqual(self.ex._stack_top(), 0)
        self.assertEqual(self.ex.execute(program, {'a': 1, 'b': 2}),
                         (1.0, 2.0, 3.0))
        self.assertEqual(self.ex._stack_top(), 0)

    def test_basics3(self):
        program = """
            foo = {}
            while #foo < 5 do
                foo[#foo+1] = #foo+1
            end
            return foo
        """
        self.assertEqual(self.ex._stack_top(), 0)
        self.assertEqual(self.ex.execute(program, {}),
                         ({1.0: 1.0,
                           2.0: 2.0,
                           3.0: 3.0,
                           4.0: 4.0,
                           5.0: 5.0},))
        self.assertEqual(self.ex._stack_top(), 0)

    def test_serialize_deserialize(self):
        program = """
            return foo
        """
        input_data = {
            1: 2,
            2: 3,
            4: "abc",
            # 5: ['a', 'b', 'c'], # TODO
            6: 6.5,
            7: None,
            8: '',
            9: True,
            10: False,
            11: {'a': {1: 2}}
        }
        expected_output = {
            1.0: 2.0,
            2.0: 3.0,
            4.0: "abc",
            # 5.0: {1.0: 'a', 2.0: 'b', 3.0: 'c'},
            6.0: 6.5,
            # 7 disappears
            8.0: '',
            9.0: True,
            10.0: False,
            11.0: {'a': {1.0: 2.0}}
            }

        self.assertEqual(self.ex.execute(program,
                                   {'foo': input_data}),
                         (expected_output,))
        self.assertEqual(self.ex._stack_top(), 0)

    def test_no_weird_python_types(self):
        program = """
            return foo
        """
        self.assertEqual(self.ex._stack_top(), 0)

        with self.assertRaises(TypeError):
            self.ex.execute(program, {'foo': object()})
        self.assertEqual(self.ex._stack_top(), 0)

        with self.assertRaises(TypeError):
            self.ex.execute(program, {'foo': set()})
        self.assertEqual(self.ex._stack_top(), 0)

        with self.assertRaises(TypeError):
            self.ex.execute(program, {'foo': set(), 'bar': 'baz'})
        self.assertEqual(self.ex._stack_top(), 0)

        with self.assertRaises(TypeError):
            self.ex.execute(program, {'foo': [1, 2, 3, 4, set()]})
        self.assertEqual(self.ex._stack_top(), 0)

        with self.assertRaises(RuntimeError):
            # recursive structure
            d = {}
            d['foo'] = d
            self.ex.execute(program, {'foo': d})
        self.assertEqual(self.ex._stack_top(), 0)

    def test_no_weird_lua_types(self):
        def _tester(program, args={}):
            program = """
                return function() return 5 end
            """
            with self.assertRaises(RuntimeError):
                self.assertEqual(self.ex._stack_top(), 0)
                self.ex.execute(program, {})
                self.assertEqual(self.ex._stack_top(), 0)

        _tester("""
            return 1, function() return 5 end
        """)

        _tester("""
            return 1, {5, function() return 5 end}
        """)

        _tester("""
            return {5, {}, function() return 5 end}}
        """)

        _tester("""
            return {5, function() return 5 end}}
        """)

        _tester("""
            return {5, 6, {7, function() return 5 end}}}}
        """)

        _tester("""
            -- recursive structure
            f = {}
            f.f = f
            return f
        """)


    def test_assertions(self):
        program = """
            assert false
        """
        with self.assertRaises(RuntimeError):
            self.ex.execute(program)

        program = """
            error("nuh uh")
        """
        with self.assertRaises(RuntimeError):
            self.ex.execute(program)


class TestSafeguards(TestLuaExecution):
    def test_memory(self):

        def _tester(program):
            start_time = time.time()
            with self.assertRaises(RuntimeError):
                self.ex.execute(program)
            self.assertLess(time.time()-start_time, 1.1)

        _tester("""
            foo = {}
            while #foo < 50000000 do
                foo[#foo+1] = 1
            end
            return 1
        """)

        _tester("""
            ('lol'):rep(1e9)
        """)

    def test_timeout(self):
        def _tester(program):
            start_time = time.time()
            with self.assertRaises(RuntimeError):
                self.ex.execute(program)
            self.assertLess(time.time()-start_time, 1.1)

        _tester("""
            foo = {}
            while true do
            end
            return 1
        """)

    def test_no_print(self):
        # make sure we didn't pass any libraries to the client program
        program = """
            print(foo)
            return 0
        """
        with self.assertRaises(RuntimeError):
            self.ex.execute(program, {'foo':0})

    def test_no_regex(self):
        # there are some regex operations you can do that are super slow, so we
        # block regexes entirely
        program = """
            return string.find(("a"):rep(1e4), ".-.-.-.-b$")
        """

        started = time.time()
        with self.assertRaises(RuntimeError):
            self.ex.execute(program)

        self.assertLess(time.time() - started, 1.0)

    def test_have_good_libs(self):
        # make sure we did pass the libraries that are okay
        program = """
            math.abs(-1)
            table.sort({})
            return foo
        """
        self.ex.execute(program, {'foo': 0})


class TestLuaSandboxedExecutor(TestLuaExecution):
    def test_stack_normal(self):
        for x in range(5):
            self.assertEqual(self.ex._stack_top(), 0)

            program = """
            return 20
            """
            self.assertEqual(self.ex.execute(program, {}), (20.0,))

            self.assertEqual(self.ex._stack_top(), 0)

    def test_stack_error(self):
        ex = SimpleSandboxedExecutor()

        for x in range(5):
            self.assertEqual(ex._stack_top(), 0)

            program = """
            error("oh noes")
            """
            with self.assertRaises(RuntimeError):
                ex.execute(program, {})

            self.assertEqual(ex._stack_top(), 0)


if __name__ == '__main__':
    unittest.main()
