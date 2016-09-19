"""
Measure performance of lua_sandbox under the most common conditions

This is:

1. bring up a VM
2. load up some code in a sandbox
3. execute that loaded code over and over with different globals set in the
   sandbox
"""

import re
import timeit

from lua_sandbox.executor import Capsule
from lua_sandbox.executor import SandboxedExecutor


def simple_test(times):
    """
    See how we fare with just regular code
    """

    lua_code = """
        return string.find(thing.body, "http")
    """
    lua = SandboxedExecutor()
    loaded = lua.sandboxed_load(lua_code)
    bodies = [
        # one match one not match
        {'body': 'http://foo.com', 'other_field': {'something': 'else'}},
        {'body': 'ooh lah lah!', 'other_field': {'something': 'else'}},
    ]

    def the_test():
        for x in bodies:
            lua.sandbox['thing'] = x
            loaded()
            lua.sandbox['thing'] = None

    ti = timeit.Timer(the_test)
    print 'simple_test', ti.timeit(number=times)


def limiter_test(times):
    """
    See how we fare with the runtime limiter enabled

    luajit suffers particularly under this one
    """

    lua_code = """
        return string.find(thing.body, "http")
    """
    lua = SandboxedExecutor()
    loaded = lua.sandboxed_load(lua_code)
    bodies = [
        # one match one not match
        {'body': 'http://foo.com', 'other_field': {'something': 'else'}},
        {'body': 'ooh lah lah!', 'other_field': {'something': 'else'}},
    ]

    def the_test():
        for x in bodies:
            lua.sandbox['thing'] = x
            with lua.limit_runtime(5.0):
                loaded()
            lua.sandbox['thing'] = None

    ti = timeit.Timer(the_test)
    print 'limiter_test', ti.timeit(number=times)


def re_test(times):
    """
    See how we fare with calling Python functions
    """

    lua_code = """
        return re.match("^http[s]", thing.body)
    """
    lua = SandboxedExecutor()
    loaded = lua.sandboxed_load(lua_code)
    lua.sandbox['re'] = {'match': re.match}
    bodies = [
        # one match one not match
        {'body': 'http://foo.com', 'other_field': {'something': 'else'}},
        {'body': 'ooh lah lah!', 'other_field': {'something': 'else'}},
    ]

    def the_test():
        for x in bodies:
            lua.sandbox['thing'] = x
            loaded()
            lua.sandbox['thing'] = None

    ti = timeit.Timer(the_test)
    print 're_test', ti.timeit(number=times)


def capsule_test(times):
    """
    See how we fare with using Capsules
    """

    lua_code = """
        return string.find(thing.body, "http")
    """
    lua = SandboxedExecutor()
    loaded = lua.sandboxed_load(lua_code)
    lua.sandbox['re'] = {'match': re.match}
    bodies = [
        # one match one not match
        {'body': 'http://foo.com', 'other_field': {'something': 'else'}},
        {'body': 'ooh lah lah!', 'other_field': {'something': 'else'}},
    ]

    def the_test():
        for x in bodies:
            lua.sandbox['thing'] = Capsule(x)
            loaded()
            lua.sandbox['thing'] = None

    ti = timeit.Timer(the_test)
    print 'capsule_test', ti.timeit(number=times)


def main(times=100000):
    tests = {
        'simple_test': simple_test,
        're_test': re_test,
        'capsule_test': capsule_test,
        'limiter_test': limiter_test,
    }

    for name, fn in sorted(tests.items()):
        fn(times=times)

if __name__ == '__main__':
    main()