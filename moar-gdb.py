import gdb
from collections import defaultdict
#import blessings
import sys

str_t_info = {0: 'int32s',
              1: 'uint8s',
              2: 'rope',
              3: 'mask'}

PRETTY_WIDTH=50

MVM_GEN2_PAGE_ITEMS = 256
MVM_GEN2_BIN_BITS   = 3

class MVMStringPPrinter(object):
    def __init__(self, val, pointer = False):
        self.val = val
        self.pointer = pointer

    def stringify(self):
        stringtyp = str_t_info[int(self.val['body']['flags']) & 0b11]
        if stringtyp in ("int32s", "uint8s"):
            zero_reached = False
            data = self.val['body'][stringtyp]
            i = 0
            pieces = []
            while not zero_reached:
                pdata = int((data + i).dereference())
                if pdata == 0:
                    zero_reached = True
                else:
                    pieces.append(chr(pdata))
                i += 1
            return "".join(pieces)
        elif stringtyp == "rope":
            i = 0
            pieces = []
            data = self.val['body']['strands']
            end_reached = False
            previous_index = 0
            previous_string = None
            while not end_reached:
                strand_data = (data + i).dereference()
                if strand_data['string'] == 0:
                    end_reached = True
                    pieces.append(previous_string[1:-1])
                else:
                    the_string = strand_data['string'].dereference()
                    if previous_string is not None:
                        pieces.append(
                            str(previous_string)[1:-1][
                                int(strand_data['string_offset']) :
                                int(strand_data['compare_offset']) - previous_index]
                            )
                    previous_string = str(the_string)
                    previous_index = int(strand_data['compare_offset'])
                i = i + 1
            return "r(" + ")(".join(pieces) + ")"
        else:
            return "string of type " + stringtyp

    def to_string(self):
        if self.pointer:
            return "pointer to '" + self.stringify() + "'"
        else:
            return "'" + self.stringify() + "'"

# currently nonfunctional
class MVMObjectPPrinter(object):
    def __init__(self, val, pointer = False):
        self.val = val
        self.pointer = pointer

    def stringify(self):
        if self.pointer:
            as_mvmobject = self.val.cast("MVMObject *").dereference()
        else:
            as_mvmobject = self.val.cast("MVMObject")

        _repr = as_mvmobject['st']['REPR']

        reprname = _repr['name'].string()

        return str(self.val.type.name) + " of repr " + reprname

    def to_string(self):
        if self.pointer:
            return "pointer to " + self.stringify()
        else:
            return self.stringify()

def show_histogram(hist, sort="value"):
    if sort == "value":
        items = sorted(list(hist.iteritems()), key = lambda (k, v): -v)
    elif sort == "key":
        items = sorted(list(hist.iteritems()), key = lambda (k, v): k)
    else:
        print "sorting mode", sort, "not implemented"
    maximum = max(hist.values())
    keymax = max([len(str(key)) for key in hist.keys()])
    for key, val in items:
        print str(key).ljust(keymax + 1), ("[" + "=" * int((float(hist[key]) / maximum) * PRETTY_WIDTH)).ljust(PRETTY_WIDTH + 1), hist[key]
    print

class CommonHeapData(object):
    number_objects = None
    number_stables = None
    number_typeobs = None
    size_histogram = None
    repr_histogram = None
    opaq_histogram = None

    generation     = None

    is_diff        = None

    def __init__(self, generation):
        self.generation = generation

        self.size_histogram = defaultdict(lambda: 0)
        self.repr_histogram = defaultdict(lambda: 0)
        self.opaq_histogram = defaultdict(lambda: 0)

        self.number_objects = 0
        self.number_stables = 0
        self.number_typeobs = 0

class NurseryData(CommonHeapData):
    allocation_offs = None
    start_addr     = None
    end_addr       = None

    def __init__(self, generation, start_addr, end_addr, allocation_offs):
        super(NurseryData, self).__init__(generation)
        self.start_addr = gdb.Value(start_addr)
        self.end_addr = gdb.Value(end_addr)
        self.allocation_offs = allocation_offs

    def analyze(self, tc):
        print "starting to analyze the nursery:"
        cursor = gdb.Value(self.start_addr)
        info_step = int(self.allocation_offs - cursor) / 50
        next_info = cursor + info_step
        print "_" * 50
        while cursor < self.allocation_offs:
            stooge = cursor.cast(gdb.lookup_type("MVMObjectStooge").pointer())
            size = stooge['common']['header']['size']
            flags = stooge['common']['header']['flags']

            is_typeobj = flags & 1
            is_stable = flags & 2

            STable = stooge['common']['st'].dereference()
            if not is_stable:
                REPR = STable["REPR"]
                REPRname = REPR["name"].string()
                if is_typeobj:
                    self.number_typeobs += 1
                else:
                    self.number_objects += 1
            else:
                REPR = None
                REPRname = "STable"
                self.number_stables += 1
            cursor += size

            self.size_histogram[int(size)] += 1
            self.repr_histogram[REPRname] += 1
            if REPRname == "P6opaque":
                self.opaq_histogram[int(size)] += 1

            if cursor > next_info:
                next_info += info_step
                sys.stdout.write("-")
                sys.stdout.flush()

        print

    def summarize(self):
        print "nursery state:"
        sizes = (int(self.allocation_offs - self.start_addr), int(self.end_addr - self.allocation_offs))
        relsizes = [1.0 * size / (float(int(self.end_addr - self.start_addr))) for size in sizes]
        print "[" + "=" * int(relsizes[0] * 20) + " " * int(relsizes[1] * 20) + "] ", int(relsizes[0] * 100),"%"

        print self.number_objects, "objects;", self.number_typeobs, " type objects;", self.number_stables, " STables"

        print "sizes of objects/stables:"
        show_histogram(self.size_histogram, "key")
        print "sizes of P6opaques only:"
        show_histogram(self.opaq_histogram, "key")
        print "REPRs"
        show_histogram(self.repr_histogram)

def bucket_index_to_size(idx):
    return (idx + 1) << MVM_GEN2_BIN_BITS

class Gen2Data(CommonHeapData):
    size_bucket     = None
    length_freelist = None
    bucket_size     = None
    g2sc   = None

    def sizes(self):
        # XXX this ought to return a tuple with the sizes we
        # accept in this bucket
        return self.size_bucket

    def __init__(self, generation, gen2sizeclass, size_bucket):
        super(Gen2Data, self).__init__(generation)

        self.size_bucket = size_bucket
        self.g2sc = gen2sizeclass
        self.bucket_size = bucket_index_to_size(self.size_bucket)

    def analyze(self, tc):
        pagebuckets = [[None] * MVM_GEN2_PAGE_ITEMS for i in range(int(self.g2sc['num_pages']))]
        page_addrs  = []

        page_cursor = self.g2sc['pages']
        for page_idx in range(self.g2sc['num_pages']):
            page_addrs.append(page_cursor.dereference())
            page_cursor += 1

        free_cursor = self.g2sc['free_list']



    def summarize(self):
        pass

class HeapData(object):
    run_nursery = None
    run_gen2    = None

    generation  = None

class AnalyzeHeapCommand(gdb.Command):
    def __init__(self):
        super(AnalyzeHeapCommand, self).__init__("moar-heap", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        tc = gdb.selected_frame().read_var(arg if arg else "tc")
        if not str(tc.type).startswith("MVMThreadContext"):
            raise ValueError("Please invoke the heap analyzer command on a MVMThreadContext, usually tc.")

        # find out the GC generation we're in (just a number increasing by 1 every time we GC)
        instance = tc['instance']
        generation = instance['gc_seq_number']

        nursery = NurseryData(generation, tc['nursery_tospace'], tc['nursery_alloc_limit'], tc['nursery_alloc'])
        nursery.analyze(tc)

        print "the current generation of the gc is", generation

        nursery.summarize()

def str_lookup_function(val):
    if str(val.type) == "MVMString":
        return MVMStringPPrinter(val)
    elif str(val.type) == "MVMString *":
        return MVMStringPPrinter(val, True)

    return None

def mvmobject_lookup_function(val):
    return None
    pointer = str(val.type).endswith("*")
    if str(val.type).startswith("MVM"):
        try:
            val.cast(gdb.lookup_type("MVMObject" + (" *" if pointer else "")))
            return MVMObjectPPrinter(val, pointer)
        except Exception as e:
            print "couldn't cast this:", e
            pass
    return None

def register_printers(objfile):
    objfile.pretty_printers.append(str_lookup_function)
    print "MoarVM string pretty printer registered"
    objfile.pretty_printers.append(mvmobject_lookup_function)
    print "MoarVM Object pretty printer registered"

commands = []
def register_commands(objfile):
    commands.append(AnalyzeHeapCommand())
    print "moar-heap registered"

if __name__ == "__main__":
    register_printers(gdb.current_objfile())
    register_commands(gdb.current_objfile())
