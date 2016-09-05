lua_sandbox is a library for calling Lua code from Python that emphasises running in a sandboxed environment.

# Features

* Execute Lua user scripts from Python
* Limit libraries available to user scripts (and some sane defaults via `SandboxedExecutor`)
* Limit user script execution time
* Limit Lua memory usage
* supports lua 5.2, 5.3, and luajit (see below)

# Example:

Simple code:

```python
from lua_sandbox.executor import SandboxedExecutor

loaded = SandboxedExecutor().sandboxed_load("""
        local sum = 0
        for i = 1, 20 do
            sum = sum + i
        end
        return sum
    """)
result = loaded()[0]

print "The result is", result.to_python()
```

Prints:
```
The result is 210.0
```

# luajit support notes

lua_sandbox supports luajit 2.0 with some limitations. Runtime limiting can be defeated in some cases (e.g. if a chunk with problematic code is run without the limiter and then again under it). Additionally, the JIT compiler is turned off when running runtime limited code.