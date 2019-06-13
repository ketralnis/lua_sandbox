#!/usr/bin/env python2.7

import sys
import os
import json
import platform

from setuptools import setup, find_packages, Extension

build_config = {}

BUILDCONF = os.environ.get('LUASANDBOX_BUILDCONF', 'build.conf')

if os.path.exists(BUILDCONF):
    sys.stderr.write("building with %s\n" % (BUILDCONF,))
    with open(BUILDCONF) as f:
        build_config.update(json.loads(f.read()))

# these defaults are for Ubuntu trusty's liblua5.2-dev package install
# locations, but you can override them with a build.conf
LUA_LIB_NAME = build_config.get('lua_lib_name', 'lua5.2')
INCLUDE_DIRS = build_config.get('include_dirs', ["/usr/include/lua5.2"])
LIBRARY_DIRS = build_config.get('library_dirs', [])
EXTRA_COMPILE_ARGS = build_config.get('extra_compile_args', ["-g"])
EXTRA_LINK_ARGS = build_config.get('extra_link_args', [])

if 'jit' in LUA_LIB_NAME and platform.system() == 'Darwin':
    EXTRA_LINK_ARGS += ["-pagezero_size 10000", "-image_base 100000000"]

_executor = Extension('lua_sandbox._executor',
                      define_macros=[('MAJOR_VERSION', '2'),
                                     ('MINOR_VERSION', '0'),
                                     ('LUA_LIB_NAME', '"%s"'%LUA_LIB_NAME)
                                     ],
                      libraries=[LUA_LIB_NAME, 'm'],
                      library_dirs=LIBRARY_DIRS,
                      include_dirs=['c'] + INCLUDE_DIRS,
                      sources=['c/_executormodule.c'],
                      extra_compile_args=['-std=c99'] + EXTRA_COMPILE_ARGS,
                      extra_link_args=EXTRA_LINK_ARGS,
                      )

PACKAGE_NAME = 'lua_sandbox'
if 'jit' in LUA_LIB_NAME:
    PACKAGE_NAME = 'luajit_sandbox'

setup(
    name=PACKAGE_NAME,
    version='2.1.9',
    description='A library to run lua code inside of a sandbox from Python',
    author='David King',
    author_email='dking@ketralnis.com',
    url='https://github.com/ketralnis/lua_sandbox',
    ext_modules=[_executor],
    packages=find_packages(),
    package_data={'lua_sandbox': ['lua_sandbox/lua_utils/*.lua']},
    zip_safe=False,
    include_package_data=True,
    install_requires=[
        ""
    ],
)
