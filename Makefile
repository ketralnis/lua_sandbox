all:
	echo no default task
	false

INSTALLEDENV=.env/installed

installenv: ${INSTALLEDENV}

check_dependencies:
	which virtualenv

${INSTALLEDENV}: setup.py
	make check_dependencies
	rm -fr .env
	virtualenv .env
	.env/bin/pip install setuptools --upgrade
	make rebuild

sdist: ${INSTALLEDENV}
	.env/bin/python ./setup.py sdist

bdist: ${INSTALLEDENV}
	.env/bin/python ./setup.py bdist

rebuild:
	.env/bin/python ./setup.py develop
	touch ${INSTALLEDENV}

test: ${INSTALLEDENV}
	.env/bin/python ./setup.py test

leaktest: ${INSTALLEDENV} test
	.env/bin/pip install pympler
	LEAKTEST=true .env/bin/python -m lua_sandbox.tests.tests ${TEST} # > /dev/null 2>&1

lldbtest: ${INSTALLEDENV}
	lldb -f .env/bin/python -- -m lua_sandbox.tests.tests ${TEST}

gdbtest: ${INSTALLEDENV}
	gdb -ex=r -ex=bt --args .env/bin/python -m lua_sandbox.tests.tests ${TEST}

clean:
	find lua_sandbox -type f -name \*.pyc -delete -print

distclean: clean
	rm -rf build
	rm -rf dist
	rm -fr .env
	rm -fr cover
	rm -fr lua_sandbox.egg-info
	rm -fr deb_dist
