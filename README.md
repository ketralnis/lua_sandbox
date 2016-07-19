lua_sandbox is a library for calling Lua code from Python that emphasises
running in a sandboxed environment.

# Features

* supports lua 5.1 through 5.3, and luajit 2.0
* Sandboxing includes limiting which libraries are available to Lua code as well
  as execution time and memory usage

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

# Packaging instructions for ppa

On a suitable trusty VM, install:
``` shell
sudo apt-get install python-stdeb fakeroot python-all dput devscripts
```
Which will allow use of the [stdeb tool](https://github.com/astraw/stdeb/) for packaging up python.  The VM/host will need access to gpg keys which have permissions on the reddit ppa (or, barring that, a personal ppa should be used so that the package can be build-tested there and then copied over when successful).

```shell
# this email address must match your gpg private key
python setup.py --command-packages=stdeb.command sdist_dsc -m "Your Name <your@email.com>"

cd deb_dist/lua-sandbox-*

# Build-Depends doesn't work in the setup.cfg, so when in doubt use sed
sed -i "s/\(^Build-Depends:.*\)/\1, liblua5.2-dev/"  debian/control

# build and sign package
debuild -S
```
This will create the dsc and changes files needed to upload to the ppa, which we do last:

```
cd ..
dput ppa:reddit/ppa lua-sandbox_*build1_source.changes
```
