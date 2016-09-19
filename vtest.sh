#!/bin/sh

set -e

function vrun() {
    vagrant ssh -c "cd lua_sandbox && sudo python ./setup.py develop && $1"
}

if [ "$1" = "-d" ]; then
    vrun 'gdb $(which python) --args python -m lua_sandbox.tests.tests'
elif [ "$1" = "-l" ]; then
    vrun 'LEAKTEST=1 python -m lua_sandbox.tests.tests'
elif [ "$1" = "-p" ]; then
    vrun 'python -m lua_sandbox.tests.perf'
else
    vrun 'python -m lua_sandbox.tests.tests'
fi
