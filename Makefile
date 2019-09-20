all:
	echo no default task
	false

INSTALLEDENV=.env/installed
PYTHON=.env/bin/python

installenv: ${INSTALLEDENV}

check_dependencies:
	which virtualenv

${INSTALLEDENV}: setup.py
	make check_dependencies
	rm -fr .env
	virtualenv --python=python3.7 .env
	make rebuild

sdist: ${INSTALLEDENV}
	${PYTHON} ./setup.py sdist

rebuild:
	${PYTHON} ./setup.py install
	.env/bin/pip install -r requirements.txt
	touch ${INSTALLEDENV}

test: ${INSTALLEDENV}
	${PYTHON} -m lua_sandbox.tests.tests ${TEST}

leaktest: ${INSTALLEDENV} test
	LEAKTEST=true ${PYTHON} -m lua_sandbox.tests.tests ${TEST} > /dev/null 2>&1

lldbtest: ${INSTALLEDENV}
	lldb -f ${PYTHON} -- -m lua_sandbox.tests.tests ${TEST}

gdbtest: ${INSTALLEDENV}
	gdb -ex=r --args ${PYTHON} -m lua_sandbox.tests.tests ${TEST}

clean:
	find src \( -name \*.pyc -o -name __pycache__ \) -delete -print

distclean: clean
	rm -rf build
	rm -rf dist
	rm -fr .env
	rm -fr cover
	rm -fr lua_sandbox.egg-info
	rm -fr deb_dist
