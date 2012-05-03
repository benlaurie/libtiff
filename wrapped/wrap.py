import re
import string
import sys

argument_types = {
    "const char*": "string",
    "int": "int",
    "long": "long",
    "FD": "file_descriptor",
}

class Argument:
    def __init__(self, xtype, name):
        self.xtype = xtype
        self.name = name

    def _declare(self, out, xtype):
        if xtype == "FD":
            xtype = "int"
        out.write(xtype + " " + self.name)

    def declare(self, out):
        self._declare(out, self.xtype)

    def declare_non_const(self, out):
        if self.xtype == "const char*":
            self._declare(out, "char *")
        else:
            self._declare(out, self.xtype)

    def send(self, out):
        if self.xtype in argument_types:
            out.write("lc_write_" + argument_types[self.xtype] + "(fd, " +
                      self.name + ")")
        else:
            assert False

    def read(self, out):
        if self.xtype == "const char*":
            out.write("lc_read_" + argument_types[self.xtype] + "(fd, &" +
                      self.name + ", 512)")
        elif self.xtype in argument_types:
            out.write("lc_read_" + argument_types[self.xtype] + "(fd, &" +
                      self.name + ")")
        else:
            assert False

class Interface:
    def __init__(self, return_type, name, args):
        self.return_type = return_type
        self.name = name
        self.args = args

    def call(self, out):
        out.write(self.return_type + " " + self.name + "(int fd")
        for arg in self.args:
            out.write(", ")
            arg.declare(out)
        out.write(") {\n")
        out.write("  lc_write_string(fd, \"" + self.name + "\");\n")
        for arg in self.args:
            out.write("  ")
            arg.send(out)
            out.write(";\n")
        assert self.return_type == "void"
        out.write("  lc_read_void(fd);\n")
        out.write("}\n")

    def test(self, out, candidate):
        out.write("!strcmp(\"" + self.name + "\", " + candidate + ")")

    def call_unwrap(self, out):
        out.write("unwrap_" + self.name + "(fd)")

    def unwrap(self, out):
        out.write("void unwrap_" + self.name + "(int fd) {\n")
        for arg in self.args:
            out.write("  ")
            arg.declare_non_const(out)
            out.write(";\n")
        out.write("\n")
        for arg in self.args:
            out.write("  ")
            arg.read(out)
            out.write(";\n")
        assert self.return_type == "void"
        out.write("  " + self.name + "(")
        first = True
        for arg in self.args:
            if first:
                first = False
            else:
                out.write(", ")
            out.write(arg.name)
        out.write(");\n")
        out.write("  lc_write_void(fd);\n")
        out.write("}\n")

base = sys.argv[1]
src = base + ".cap"

decl_pat = re.compile("\s*(.+?)\s+(\S+)\((.*)\);")
arg_pat = re.compile("\s*(.*)\s+([*A-Za-z0-9_]+)\s*")
arg_fix_pat = re.compile("(\**)(.*)")

f = open(src, "r")

decls = ''.join(f.readlines())
print decls
interfaces = []
for ret, fname, args in decl_pat.findall(decls):
    print "ret =", ret, "fname =", fname, "args =", args
    arg_list = []
    if args != "":
        for arg in string.split(args, ","):
            print arg
            xtype, name = arg_pat.findall(arg)[0]
            stars, name = arg_fix_pat.findall(name)[0]
            xtype += stars
            print "xtype =", xtype, "name =", name
            arg = Argument(xtype, name)
            arg_list.append(arg)
    interface = Interface(ret, fname, arg_list)
    interfaces.append(interface)

f.close()

f = open(base + "_send.c", "w")

for interface in interfaces:
    interface.call(f)

f.close()

f = open(base + "_recv.c", "w")

for interface in interfaces:
    interface.unwrap(f)

f.write("""
void dispatch(int fd) {
  for ( ; ; ) {
    char *cmd;

    lc_read_string(fd, &cmd, 100);
""")

first = True
for interface in interfaces:
    if first:
        f.write("    if (")
        first = False
    else:
        f.write("    else if (")
    interface.test(f, "cmd")
    f.write(")\n      ")
    interface.call_unwrap(f)
    f.write(";\n")
f.write("  }\n")
f.write("}\n")
f.close()

