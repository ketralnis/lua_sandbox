from lua_sandbox._executor import _LuaExecutor
from lua_sandbox.utils import list_to_table, datafile


class LuaExecutor(_LuaExecutor):
    pass


class SandboxedExecutor(LuaExecutor):
    def execute(self, sandboxer, program, env=None, desc=None):
        libs = list_to_table([program])
        return _LuaExecutor.execute(self,
                                    sandboxer,
                                    {'code': libs,
                                     'env': env,
                                     'desc': desc})


class SimpleSandboxedExecutor(LuaExecutor):
    sandboxer = datafile('lua_utils/safe_sandbox.lua')

    def execute(self, program, env=None, desc=None):
        libs = list_to_table([program])
        return _LuaExecutor.execute(self,
                                    self.sandboxer,
                                    {'code': libs,
                                     'env': env,
                                     'desc': desc})
