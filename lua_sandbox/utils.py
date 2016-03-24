import os.path

def datafile(name, filecache={}):
    if name not in filecache:
        filecache[name] = open(os.path.join(os.path.dirname(__file__), name)).read()
    return filecache[name]

def list_to_table(l):
    return {i+1: itm for i,itm in enumerate(l)}
