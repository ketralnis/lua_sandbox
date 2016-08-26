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
	virtualenv-2.7 .env
	make rebuild

sdist: ${INSTALLEDENV}
	.env/bin/python ./setup.py sdist

rebuild:
	.env/bin/python ./setup.py develop
	touch ${INSTALLEDENV}

test: ${INSTALLEDENV}
	.env/bin/python -m lua_sandbox.tests.tests ${TEST}

leaktest: ${INSTALLEDENV} test
	LEAKTEST=true .env/bin/python -m lua_sandbox.tests.tests ${TEST} > /dev/null 2>&1

gtest: ${INSTALLEDENV}
	lldb -f .env/bin/python -- -m lua_sandbox.tests.tests ${TEST}

clean:
	find lua_sandbox -type f -name \*.pyc -delete -print

distclean: clean
	rm -rf build # only to deal with old style installs
	rm -fr .env
	rm -fr node_modules
	rm -fr cover
	rm -fr lua_sandbox.egg-info
