# Packaging instructions for ppa

On a suitable trusty VM, install the packages (the Vagrant VM does this for you)

```shell
sudo apt-get install python-stdeb fakeroot python-all dput devscripts
```

This will allow use of the [stdeb tool](https://github.com/astraw/stdeb/) for
packaging up python.  The VM/host will need access to gpg keys which have
permissions on the reddit ppa (or, barring that, a personal ppa should be used
so that the package can be build-tested there and then copied over when
successful).

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

```shell
cd ..
dput ppa:reddit/ppa lua-sandbox_*build1_source.changes
```
