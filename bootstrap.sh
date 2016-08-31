#!/bin/bash

# bootstrapper for Vagrant

set -ev

apt-get update

# the basics required to get lua_sandbox compiling
apt-get -y install python python-pip python-dev liblua5.2-dev

# I use this VM for packaging too so add that stuff in while we're at it. you
# can comment it out if it slows stuff down for you a lot
apt-get -y install python-stdeb fakeroot python-all dput devscripts

cd /home/vagrant/lua_sandbox
./setup.py develop

# make sure everything seemed to work!
python -m lua_sandbox.tests.tests
