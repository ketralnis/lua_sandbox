lua_sandbox is a library for calling Lua code from Python that emphasises
running in a sandboxed environment.

# Features

* Execute Lua user scripts from Python
* Limit libraries available to user scripts (and some sane defaults via `SimpleSandboxedExecutor`)
* Limit user script execution time
* Limit Lua memory usage
* supports lua 5.1 through 5.3, and luajit 2.0

# Example:

Simple code:

```python
    from lua_sandbox.executor import SimpleSandboxedExecutor

    result = SimpleSandboxedExecutor().execute("""
        local sum = 0
        for i = 1, 20 do
            sum = sum + i
        end
        return sum
    """)[0]

    print "The result is", result
```

Prints:
```
    The result is 210.0
```
