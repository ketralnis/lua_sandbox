#!/bin/sh

set -ev

if [ "$1" = "-d" ]; then
    vagrant ssh -c 'cd lua_sandbox && sudo python ./setup.py develop && gdb $(which python) --args python -m lua_sandbox.tests.tests'
elif [ "$1" = "-l" ]; then
    vagrant ssh -c 'cd lua_sandbox && sudo python ./setup.py develop && LEAKTEST=1 python -m lua_sandbox.tests.tests'
else
    vagrant ssh -c 'cd lua_sandbox && sudo python ./setup.py develop && python -m lua_sandbox.tests.tests'
fi
