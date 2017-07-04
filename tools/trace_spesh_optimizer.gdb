define trace_spesh_optimize
    dont-repeat
    python

import tempfile
import os
import gdb
spesh_trace_target_folder = tempfile.mkdtemp("", "moar-spesh-trace")
spesh_trace_target_file = os.path.join(spesh_trace_target_folder, "speshdump.txt")
os.system("git init " + spesh_trace_target_folder)

env_vars = "env GIT_DIR=" + os.path.join(spesh_trace_target_folder, ".git") + " GIT_WORK_TREE=" + spesh_trace_target_folder

temp_breakpoints = []

class DumpingResumingBreakpoint(gdb.Breakpoint):
    def __init__(self, message, *args):
        gdb.Breakpoint.__init__(self, *args)
        self.message = message
        self.andthen = lambda self: ""

    def stop(self):
        dump_to_file_and_commit(self.message + self.andthen(self))
        return False # don't actually stop

    def andthen(self, fn):
        self.andthen = fn

def add_dump_breakpoint(spec, message = "."):
    bp = DumpingResumingBreakpoint(message, spec)
    temp_breakpoints.append(bp)
    return bp

def cleanup_breakpoints():
    global temp_breakpoints
    for bp in temp_breakpoints:
        bp.delete()
    temp_breakpoints = []

def dump_to_file_and_commit(message = "."):
    histnum = gdb.execute('''call MVM_spesh_dump(tc, g)''', True, True).split(" = ")[0].lstrip("$")
    dump_output = gdb.history(int(histnum)).string()
    with open(spesh_trace_target_file, "w") as f:
        f.write(dump_output)
    os.system(env_vars + " git add " + os.path.join(spesh_trace_target_folder, "speshdump.txt"))
    os.system(env_vars + " git commit -m '" + message + "'")

add_dump_breakpoint("MVM_spesh_facts_discover", "before everything")
add_dump_breakpoint("MVM_spesh_optimize", "after facts have been discovered")
add_dump_breakpoint("eliminate_dead_bbs", "before eliminating dead BBs")
add_dump_breakpoint("optimize_bb", "going to optimize a bb").andthen(lambda self: gdb.execute("print bb->idx", False, True))
add_dump_breakpoint("eliminate_unused_log_guards", "eliminating unused log guards")
add_dump_breakpoint("eliminate_pointless_gotos", "eliminating pointless gotos")
add_dump_breakpoint("eliminate_dead_ins", "eliminating dead instructions")
add_dump_breakpoint("second_pass", "starting second pass")
add_dump_breakpoint("MVM_spesh_codegen", "done!")

gdb.execute("finish")

os.system("env GIT_DIR=" + os.path.join(spesh_trace_target_folder, ".git") +
          " git log -u")

cleanup_breakpoints()

    end
end
