define trace_spesh_optimize
    dont-repeat
    python

#
# This script lets you see a step-by-step listing of what spesh
# does to one particular frame.
#
# How to use it
# =============
#
# source this from your ~/.gdbinit or directly from your gdb shell
#
# Reach the beginning of MVM_spesh_candidate_add (for example by setting
# a breakpoint to that function)
#
# call trace_spesh_optimize from your gdb commandline
#
# Wait for a little bit.
#

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

earliest_dump_output = ""

def dump_to_file_and_commit(message = "."):
    global earliest_dump_output

    histnum = gdb.execute('''call MVM_spesh_dump(tc, g)''', True, True).split(" = ")[0].lstrip("$")
    dump_output = gdb.history(int(histnum)).string()

    if earliest_dump_output == "":
        earliest_dump_output = dump_output

    if dump_output.split("\n")[0] == earliest_dump_output.split("\n")[0]:
        with open(spesh_trace_target_file, "w") as f:
            f.write(dump_output)
    os.system(env_vars + " git add " + os.path.join(spesh_trace_target_folder, "speshdump.txt"))
    os.system(env_vars + " git commit -m '" + message + "'")

try:
    add_dump_breakpoint("MVM_spesh_args", "before specializing for args")
    add_dump_breakpoint("MVM_spesh_facts_discover", "before spesh")
    add_dump_breakpoint("MVM_spesh_optimize", "after facts have been discovered")
    add_dump_breakpoint("MVM_spesh_eliminate_dead_bbs", "before eliminating dead BBs")
    add_dump_breakpoint("optimize_bb_switch", "going to optimize a bb").andthen(lambda self: gdb.execute("print bb->idx", False, True))
    add_dump_breakpoint("post_inline_visit_bb", "going to optimize a bb").andthen(lambda self: gdb.execute("print bb->idx", False, True))
    add_dump_breakpoint("eliminate_unused_log_guards", "eliminating unused log guards")
    add_dump_breakpoint("eliminate_pointless_gotos", "eliminating pointless gotos")
    add_dump_breakpoint("MVM_spesh_usages_remove_unused_deopt", "removing unused deopts")
    add_dump_breakpoint("MVM_spesh_eliminate_dead_ins", "eliminating dead instructions")
    add_dump_breakpoint("post_inline_pass", "starting second pass")
    add_dump_breakpoint("MVM_spesh_codegen", "done!")

    gdb.execute("finish")

    os.system("env GIT_DIR=" + os.path.join(spesh_trace_target_folder, ".git") +
              " git log --reverse -u")
finally:
    cleanup_breakpoints()

    print("GIT_DIR was set to ", os.path.join(os.path.join(spesh_trace_target_folder, ".git")))

    end
end
