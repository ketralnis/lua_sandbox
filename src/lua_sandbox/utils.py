import os.path


def dataloc(name):
    return os.path.join(os.path.dirname(__file__), name)


def datafile(name, filecache={}):
    if name not in filecache:
        filecache[name] = open(dataloc(name), 'rb').read()
    return filecache[name]
