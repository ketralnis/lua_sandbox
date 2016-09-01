#!/bin/bash

# create an ubuntu ppa package. this is intended to be run from the instance
# created by the Vagrantfile so you may need to modify it if that's not you

set -e

function confirm_or_die() {
    echo "$1"

    read -p '[y/n] ' -sn1 r
    echo ''

    if [ "$r" = "y" ]; then
        return
    elif [ "$r" = "n" ]; then
        echo "$2"
        exit 1
    else
        echo "wtf is a $r?"
        exit 1
    fi
}

cd ~/lua_sandbox

if git status --porcelain | grep .; then
    echo "it looks like there are dirty files. fix that first"
    exit 1
fi

if ! [ "$(git symbolic-ref --short HEAD)" = "master" ]; then
    echo "you probably mean to be on master, not $(git symbolic-ref --short HEAD)"
    exit 1
fi

if [ -z "$AUTHOR" ]; then
    AUTHOR="$(git --no-pager log -1 --pretty=format:"%an <%ae>" HEAD)"
fi

if [ -z "$PPA" ]; then
    PPA="ppa:ketralnis/test-ppa"
fi

confirm_or_die "building package by $AUTHOR is that you?" \
    'set $AUTHOR and try again'

if ! gpg -K | fgrep "$AUTHOR"; then
    echo "gpg key for $AUTHOR not found in ~/.gnupg/secring.gpg"
    echo "here's a handy snippet for vagrant:"
    echo "    (cd ~ && tar -cvf - .gnupg) | vagrant ssh -c 'cd ~ && tar -xvf -'"
    exit 1
fi

echo cloning to /tmp

rm -vfr /tmp/lua_sandbox
cd /tmp
git clone --depth=1 --branch=master ~/lua_sandbox/.git lua_sandbox
rm -fr /tmp/lua_sandbox/.git
cd /tmp/lua_sandbox
find .

python setup.py --command-packages=stdeb.command sdist_dsc -m "$AUTHOR"

cd deb_dist/lua-sandbox-*

# Build-Depends doesn't work in the setup.cfg, so when in doubt use sed
sed -i "s/\(^Build-Depends:.*\)/\1, liblua5.2-dev/"  debian/control

# build and sign package
debuild -S

cd ..

confirm_or_die "Will upload to $PPA. Is that right?" \
    'set $PPA and try again'

dput $PPA lua-sandbox_*build1_source.changes
