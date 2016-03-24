#ifndef _EXECUTOR_MODULE_H
#define _EXECUTOR_MODULE_H

#if PY_VERSION_HEX < 0x02070000
#error "We require Python 2.7 to run"
#endif

PyMODINIT_FUNC init_executor(void);

#endif /* _EXECUTOR_MODULE_H */
