import re
import string
import sys

argument_types = {
    "const char*": "string",
    "int": "int",
    "long": "long",
    "FD": "file_descriptor",
    "uint32": "uint32_t",
    "uint16": "uint16_t",
    "size_t": "size_t",
}

va_typemap = {
    "uint16": "int",
}

decl_pat = re.compile("\s*(.+?)\s+([A-Za-z0-9_]+)\((.*?)\)([^;]*);")
arg_pat = re.compile("^\s*(.*)\s+([*A-Za-z0-9_]+)\s*(|\[[a-z]+\])\s*$")
arg_fix_pat = re.compile("(\**)(.*)")

def parse_args(args, extras):
    arg_list = []
    if args != "":
        for arg in string.split(args, ","):
            print "arg =", arg
            xtype, name, dim = arg_pat.findall(arg)[0]
            stars, name = arg_fix_pat.findall(name)[0]
            xtype += stars
            print "xtype =", xtype, "name =", name, "dim =", dim
            arg = Argument(xtype, name, dim, extras)
            arg_list.append(arg)
    return arg_list

va_pat = re.compile("\s*(.+?)\s*:\s*\((.*)\)")

class VAArgument:
    def __init__(self, cond, args):
        self.cond = cond
        self.args = parse_args(args, None)

    def _test(self, out):
        out.write("if (" + self.cond + ") {\n");

    def send(self, out, ap):
        self._test(out)
        for arg in self.args:
            out.write("    ")
            arg.va_declare(out, ap)
            out.write(";\n")
        out.write("\n")
        for arg in self.args:
            out.write("    ")
            arg.send(out)
            out.write(";\n")
        out.write("  }")

    def read(self, out, interface):
        self._test(out)
        for arg in self.args:
            out.write("    ")
            arg.declare(out)
            out.write(";\n")
        for arg in self.args:
            out.write("    ")
            arg.read(out, interface)
            out.write(";\n")
        out.write("    ")
        interface.indirect_call_prefix(out)
        first = True
        for arg in self.args:
            if first:
                first = False
            else:
                out.write(", ")
            out.write(arg.name)
        out.write(");\n  }")

dim_pat = re.compile("\[(.*)\]")

class Argument:
    def __init__(self, xtype, name, dim, extras):
        self.xtype = xtype
        self.name = name
        self.dim = ""
        if dim != "":
            self.dim = dim_pat.findall(dim)[0]
        if self.xtype == "va_list":
            self._compile_va_list(extras)

    def _compile_va_list(self, str):
        self.va_list = []
        for item in str.split("\n"):
            for cond, args in va_pat.findall(item):
                self.va_list.append(VAArgument(cond, args))

    def _declare(self, out, xtype):
        if xtype == "FD":
            xtype = "int"
        if self.dim != "":
            out.write(xtype + " *" + self.name)
        else:
            out.write(xtype + " " + self.name)

    def declare(self, out):
        self._declare(out, self.xtype)

    def va_declare(self, out, ap):
        self._declare(out, self.xtype)
        xtype = self.xtype
        if xtype in va_typemap:
            xtype = va_typemap[xtype]
        out.write(" = va_arg(" + ap + ", " + xtype + ")")

    def declare_non_const(self, out):
        if self.xtype == "const char*":
            self._declare(out, "char *")
        else:
            self._declare(out, self.xtype)

    def send_va_list(self, out):
        first = True
        for va in self.va_list:
            if first:
                first = False
            else:
                out.write(" else ")
            va.send(out, self.name)

    def send(self, out):
        print self.xtype
        if self.xtype == "va_list":
            self.send_va_list(out)
        elif self.xtype == "char" and self.dim != "":
            out.write("lc_write_" + self.xtype + "_array(fd, " + self.name +
                      ", " + self.dim + ")")
        elif self.xtype in argument_types:
            out.write("lc_write_" + argument_types[self.xtype] + "(fd, " +
                      self.name + ")")
        else:
            assert False

    def read_va_list(self, out, interface):
        first = True
        for va in self.va_list:
            if first:
                first = False
            else:
                out.write(" else ")
            va.read(out, interface)

    def read(self, out, interface):
        if self.xtype == "const char*":
            out.write("lc_read_" + argument_types[self.xtype] + "(fd, &" +
                      self.name + ", 512)")
        elif self.xtype == "va_list":
            self.read_va_list(out, interface)
        elif self.xtype == "char" and self.dim != "":
            # FIXME: should we instead read an array and a length?
            out.write("lc_read_char_array(fd, &" + self.name + ", " +
                      self.dim + ")")
        elif self.xtype in argument_types:
            out.write("lc_read_" + argument_types[self.xtype] + "(fd, &" +
                      self.name + ")")
        else:
            assert False

class Interface:
    def __init__(self, return_type, name, args, extras):
        self.return_type = return_type
        self.name = name
        self.args = args
        self.extras = extras

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
        if self.return_type == "void":
            out.write("  lc_read_void(fd);\n")
        else:
            out.write("  " + self.return_type + " ret;\n")
            out.write("  lc_read_" + self.return_type + "(fd, &ret);\n")
            out.write("  return ret;\n")
        out.write("}\n")

    def test(self, out, candidate):
        out.write("!strcmp(\"" + self.name + "\", " + candidate + ")")

    def call_unwrap(self, out):
        out.write("unwrap_" + self.name + "(fd)")

    def indirect(self, out):
        out.write(self.return_type + " indirect_" + self.name + "(")
        first = True
        for arg in self.args:
            if arg.xtype == "va_list":
                break
            if first:
                first = False
            else:
                out.write(", ")
            arg.declare(out)
            last = arg.name
        out.write(", ...) {\n")
        out.write("  va_list ap;\n")
        out.write("  va_start(ap, " + last + ");\n")
        out.write("  return " + self.name + "(")
        for arg in self.args:
            if arg.xtype == "va_list":
                break
            out.write(arg.name + ", ")
        out.write("ap);\n")
        out.write("}\n")

    def unwrap(self, out):
        need_indirect = False
        for arg in self.args:
            if arg.xtype == "va_list":
                need_indirect = True
                break
        if need_indirect:
            self.indirect(out)
        out.write("void unwrap_" + self.name + "(int fd) {\n")
        for arg in self.args:
            if arg.xtype == "va_list":
                break
            out.write("  ")
            arg.declare_non_const(out)
            out.write(";\n")
        if self.return_type != "void":
            out.write("  " + self.return_type + " ret;\n")
        out.write("\n")
        for arg in self.args:
            out.write("  ")
            arg.read(out, self)
            out.write(";\n")
        if not need_indirect:
            out.write("  ")
            if self.return_type != "void":
                out.write("ret = ")
            out.write(self.name + "(")
            first = True
            for arg in self.args:
                if first:
                    first = False
                else:
                    out.write(", ")
                out.write(arg.name)
            out.write(");\n")
        if self.return_type == "void":
            out.write("  lc_write_void(fd);\n")
        else:
            out.write("  lc_write_" + self.return_type + "(fd, ret); \n")
        out.write("}\n")

    def indirect_call_prefix(self, out):
        if self.return_type != "void":
            out.write("ret = ")
        out.write("indirect_" + self.name + "(")
        for arg in self.args:
            if arg.xtype == "va_list":
                break
            out.write(arg.name)
            out.write(", ")

base = sys.argv[1]
src = base + ".cap"

f = open(src, "r")

decls = ''.join(f.readlines())
print decls
interfaces = []
for ret, fname, args, extras in decl_pat.findall(decls):
    print "ret =", ret, "fname =", fname, "args =", args, "extras =", extras
    arg_list = parse_args(args, extras)
    interface = Interface(ret, fname, arg_list, extras)
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
f.write("    else {\n")
f.write("      fprintf(stderr, \"unknown capability: %s\\n\", cmd);\n")
f.write("      exit(1);\n")
f.write("    }\n")
f.write("  free(cmd);\n")
f.write("  }\n")
f.write("}\n")
f.close()

