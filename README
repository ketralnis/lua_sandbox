lua_sandbox is a library for calling Lua code from Python that emphasises
running in a sandboxed environment.

# Features

* supports lua 5.1 through 5.3, and luajit 2.0
* Sandboxing includes limiting which libraries are available to Lua code as well
  as execution time and memory usage

# Example:

Simple code:

    from lua_sandbox.executor import SimpleSandboxedExecutor

    result = SimpleSandboxedExecutor().execute("""
        local sum = 0
        for i = 1, 20 do
            sum = sum + i
        end
        return sum
    """)[0]

    print "The result is", result

Prints:

    The result is 210.0
