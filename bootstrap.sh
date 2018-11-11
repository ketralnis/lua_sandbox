#!/bin/bash

# bootstrapper for Vagrant

set -ev

apt-get update

if [ -e /home/vagrant/lua_sandbox/build.conf ] && \
    grep luajit /home/vagrant/lua_sandbox/build.conf
then
    USE_LUA_LIB="libluajit-5.1-dev"
else
    USE_LUA_LIB="liblua5.2-dev liblua5.2-0-dbg"
fi

# the basics required to get lua_sandbox compiling
apt-get -y install python python-pip python-dev python-virtualenv $USE_LUA_LIB

# development conveniences
apt-get -y install gdb python-all-dbg
pip install pympler

# I use this VM for packaging too so add that stuff in while we're at it. you
# can comment it out if it slows stuff down for you a lot
apt-get -y install python-stdeb fakeroot python-all dput devscripts git \
    python-all-dev gnupg

cd /home/vagrant/lua_sandbox
./setup.py develop

# make sure everything seemed to work!
python -m lua_sandbox.tests.tests
