all:
	echo no default task
	false

INSTALLEDENV=.env/installed

installenv: ${INSTALLEDENV}

check_dependencies:
	which lua
	which virtualenv

${INSTALLEDENV}: setup.py
	make check_dependencies
	rm -fr .env
	virtualenv .env
	make rebuild

rebuild:
	.env/bin/python ./setup.py develop
	touch ${INSTALLEDENV}

test tests: ${INSTALLEDENV}
	.env/bin/python -m lua_sandbox.tests.tests

clean:
	find lua_sandbox -type f -name \*.pyc -delete -print

distclean: clean
	rm -rf build # only to deal with old style installs
	rm -fr .env
	rm -fr node_modules
	rm -fr cover
	rm -fr lua_sandbox.egg-info
