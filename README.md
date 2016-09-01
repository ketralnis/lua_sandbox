lua_sandbox is a library for calling Lua code from Python that emphasises
running in a sandboxed environment.

# Features

* Execute Lua user scripts from Python
* Limit libraries available to user scripts (and some sane defaults via `SandboxedExecutor`)
* Limit user script execution time
* Limit Lua memory usage
* supports 5.2 and 5.3 (support for loadjit is currently not implemented)

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
