#!/bin/bash

# bootstrapper for Vagrant

set -ev

apt-get update

LHOME=/home/vagrant/lua_sandbox/

if [ -e $LHOME/build.conf ] && \
    grep luajit $LHOME/build.conf
then
    echo 'using luajit'
    USE_LUA_LIB="libluajit-5.1-dev"
else
    echo 'using lua 5.2'
    USE_LUA_LIB="liblua5.2-dev liblua5.2-0-dbg"
fi

# the basics required to get lua_sandbox compiling
apt-get -y install python3.7 python3.7-dev virtualenv make gcc $USE_LUA_LIB

cd $LHOME
make installenv

# development conveniences
# apt-get -y install gdb python3.7-dbg libpython3.7-dbg
$LHOME/.env/bin/pip install pympler

# make sure everything seemed to work!
make test
