import itertools

from lua_sandbox._executor import _LuaExecutor
from lua_sandbox._executor import LuaException
from lua_sandbox._executor import LuaOutOfMemoryException
from lua_sandbox.utils import list_to_table, datafile


class LuaExecutor(_LuaExecutor):
    pass


class SandboxedExecutor(LuaExecutor):
    def execute(self, sandboxer, program, libs=(), env=None, desc=None):
        libs = list_to_table(itertools.chain(libs, (program,)))
        return _LuaExecutor.execute(self,
                                    sandboxer,
                                    {'code': libs,
                                     'env': env,
                                     'desc': desc})


class SimpleSandboxedExecutor(SandboxedExecutor):
    sandboxer = datafile('lua_utils/safe_sandbox.lua')

    def execute(self, program, libs=(), env=None, desc=None):
        sup = super(SimpleSandboxedExecutor, self)
        return sup.execute(self.sandboxer, program, libs=libs,
                           env=env, desc=desc)
