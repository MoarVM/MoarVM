# -*- coding: utf8 -*-
import gdb
from collections import defaultdict
from itertools import chain
import math
import random
#import blessings
import sys

str_t_info = {0: 'int32s',
              1: 'uint8s',
              2: 'rope',
              3: 'mask'}

PRETTY_WIDTH=50

MVM_GEN2_PAGE_ITEMS = 256
MVM_GEN2_BIN_BITS   = 3
MVM_GEN2_BINS       = 32

MVM_GEN2_PAGE_CUBE_SIZE = int(math.sqrt(MVM_GEN2_PAGE_ITEMS))

EXTRA_SAMPLES = 8

array_storage_types = [
        'obj',
        'str',
        'i64', 'i32', 'i16', 'i8',
        'n64', 'n32',
        'u64', 'u32', 'u16', 'u8'
        ]

halfblocks = u"█▀▄ ░▒▓"

def shade_block(u, d):
    if u == d == True:
        return halfblocks[0]
    elif u == True and d == False:
        return halfblocks[1]
    elif u == False and d == True:
        return halfblocks[2]
    elif u == d == False:
        return halfblocks[3]
    elif u == d == None:
        return halfblocks[4]
    elif u == None and d == False or u == False and d == None:
        return halfblocks[5]
    elif u == None and d == True or u == True and d == None:
        return halfblocks[6]


def generate_hilbert(amount):
    hilbert_coords = []

    # adapted from the english wikipedia article on the Hilbert Curve
    n = int(math.sqrt(amount))
    def rot(s, x, y, rx, ry):
        if ry == 0:
            if rx == 1:
                x = s - 1 - x
                y = s - 1 - y
            return (y, x)
        return (x, y)

    def xy2d(x, y):
        rx = 0
        ry = 0
        d = 0

        s = int(n / 2)
        while s > 0:
            rx = (x & s) > 0
            ry = (y & s) > 0
            d += s * s * ((3 * rx) ^ ry)
            (x, y) = rot(s, x, y, rx, ry)
            s /= 2

        return d

    for y in range(n):
        hilbert_coords.append([])
        for x in range(n):
            hilbert_coords[-1].append(xy2d(x, y))

    return hilbert_coords

hilbert_coords = generate_hilbert(MVM_GEN2_PAGE_ITEMS)

def prettify_size(num):
    rest = str(num)
    result = ""
    while len(rest) > 3:
        result = rest[-3:] + "." + result
        rest = rest[:-3]
    if len(rest) >= 1:
        result = rest + "." + result
    return result[:-1]

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

def show_histogram(hist, sort="value", multiply=False):
    if len(hist) == 0:
        print "(empty histogram)"
        return
    if sort == "value":
        items = sorted(list(hist.iteritems()), key = lambda (k, v): -v)
    elif sort == "key":
        items = sorted(list(hist.iteritems()), key = lambda (k, v): k)
    else:
        print "sorting mode", sort, "not implemented"
    maximum = max(hist.values())
    keymax = max([len(str(key)) for key in hist.keys()])
    for key, val in items:
        appendix = prettify_size(int(key) * int(val)).rjust(10) if multiply else ""
        print str(key).ljust(keymax + 1), ("[" + "=" * int((float(hist[key]) / maximum) * PRETTY_WIDTH)).ljust(PRETTY_WIDTH + 1), str(val).ljust(len(str(maximum)) + 2), appendix
    print

def diff_histogram(hist_before, hist_after, sort="value", multiply=False):
    max_hist = defaultdict(lambda: 0)
    min_hist = defaultdict(lambda: 0)
    zip_hist = {}
    max_val = 0
    max_key = 0
    longest_key = ""
    for k,v in chain(hist_before.iteritems(), hist_after.iteritems()):
        max_hist[k] = max(max_hist[k], v)
        min_hist[k] = min(max_hist[k], v)
        max_val = max(max_val, v)
        max_key = max(max_key, k)
        longest_key = str(k) if len(str(k)) > len(longest_key) else longest_key

    for k in max_hist.keys():
        zip_hist[k] = (hist_before[k], hist_after[k])

    if sort == "value":
        items = sorted(list(zip_hist.iteritems()), key = lambda (k, (v1, v2)): -max(v1, v2))
    elif sort == "key":
        items = sorted(list(zip_hist.iteritems()), key = lambda (k, (v1, v2)): k)
    else:
        print "sorting mode", sort, "not implemented"

    for key, (val1, val2) in items:
        lv, rv = min(val1, val2), max(val1, val2)
        lc, rc = ("+", "-") if val1 > val2 else ("-", "+")
        bars = "[" \
               + lc * int((float(lv) / max_val) * PRETTY_WIDTH) \
               + rc * int((float(rv - lv) / max_val) * PRETTY_WIDTH)

        values = str(val1).ljust(len(str(max_val)) + 1) \
                + " -> " \
                + str(val2).ljust(len(str(max_val)) + 1)

        appendix = prettify_size(int(key) * int(val1)).rjust(10) \
                   + " -> " \
                   + prettify_size(int(key) * int(val2)).rjust(10) \
                       if multiply else ""

        print str(key).ljust(len(longest_key) + 2), bars.ljust(PRETTY_WIDTH), values, appendix

class CommonHeapData(object):
    number_objects = None
    number_stables = None
    number_typeobs = None
    size_histogram = None
    repr_histogram = None
    opaq_histogram = None
    arrstr_hist    = None
    arrusg_hist    = None

    generation     = None

    def __init__(self, generation):
        self.generation = generation

        self.size_histogram = defaultdict(lambda: 0)
        self.repr_histogram = defaultdict(lambda: 0)
        self.opaq_histogram = defaultdict(lambda: 0)
        self.arrstr_hist    = defaultdict(lambda: 0)
        self.arrusg_hist    = defaultdict(lambda: 0)

        self.number_objects = 0
        self.number_stables = 0
        self.number_typeobs = 0

    def analyze_single_object(self, cursor):
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

        self.size_histogram[int(size)] += 1
        self.repr_histogram[REPRname] += 1
        if REPRname == "P6opaque":
            self.opaq_histogram[int(size)] += 1
        elif REPRname == "VMArray":
            slot_type = int(STable['REPR_data'].cast(gdb.lookup_type("MVMArrayREPRData").pointer())['slot_type'])
            self.arrstr_hist[array_storage_types[slot_type]] += 1
            array_body = cursor.cast(gdb.lookup_type("MVMArray").pointer())['body']
            if array_body['ssize'] == 0:
                usage_perc = "N/A"
            else:
                usage_perc = (int(array_body['elems'] * 10) / int(array_body['ssize'])) * 10
                if usage_perc < 0 or usage_perc > 100:
                    usage_perc = "inv"
            self.arrusg_hist[usage_perc] += 1

        return size

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
            size = self.analyze_single_object(cursor)

            cursor += size
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
        show_histogram(self.size_histogram, "key", True)
        print "sizes of P6opaques only:"
        show_histogram(self.opaq_histogram, "key", True)
        print "REPRs:"
        show_histogram(self.repr_histogram)
        print "VMArray storage types:"
        show_histogram(self.arrstr_hist)
        print "VMArray usage percentages:"
        show_histogram(self.arrusg_hist, "key")

    def diff(self, other):
        print "nursery state --DIFF--:"

        print "sizes of objects/stables:"
        diff_histogram(self.size_histogram, other.size_histogram, "key", True)
        print "sizes of P6opaques only:"
        diff_histogram(self.opaq_histogram, other.opaq_histogram, "key", True)
        print "REPRs:"
        diff_histogram(self.repr_histogram, other.repr_histogram)
        print "VMArray storage types:"
        diff_histogram(self.arrstr_hist, other.arrstr_hist)
        print "VMArray usage percentages:"
        diff_histogram(self.arrusg_hist, other.arrusg_hist, "key")


def bucket_index_to_size(idx):
    return (idx + 1) << MVM_GEN2_BIN_BITS

class Gen2Data(CommonHeapData):
    size_bucket     = None
    length_freelist = None
    bucket_size     = None
    g2sc   = None

    page_addrs  = None
    pagebuckets = None
    empty       = False
    cur_page    = 0

    repr_histogram = None
    size_histogram = None

    def sizes(self):
        # XXX this ought to return a tuple with the sizes we
        # accept in this bucket
        return bucket_index_to_size(self.size_bucket), bucket_index_to_size(self.size_bucket + 1) - 1

    def __init__(self, generation, gen2sizeclass, size_bucket):
        super(Gen2Data, self).__init__(generation)

        self.size_bucket = size_bucket
        self.g2sc = gen2sizeclass
        self.bucket_size = bucket_index_to_size(self.size_bucket)
        self.repr_histogram = defaultdict(lambda: 0)
        self.size_histogram = defaultdict(lambda: 0)

    def analyze(self, tc):
        if int(self.g2sc['pages'].cast(gdb.lookup_type("int"))) == 0:
            self.empty = True
            return
        pagebuckets = [[True for i in range(MVM_GEN2_PAGE_ITEMS)] for i in range(int(self.g2sc['num_pages']))]
        page_addrs  = []

        self.page_addrs = page_addrs
        self.pagebuckets = pagebuckets

        self.cur_page = int(self.g2sc['cur_page'])

        # we need to make sure we don't accidentally run into free_list'd slots
        # that's why we just collect addresses up front and sample them later on.
        sample_stooges = []

        page_cursor = self.g2sc['pages']
        for page_idx in range(self.g2sc['num_pages']):
            page_addrs.append(page_cursor.dereference())
            if page_idx == self.cur_page:
                alloc_bucket_idx = int(int(self.g2sc['alloc_pos'] - page_cursor.dereference()) / self.bucket_size)
                pagebuckets[page_idx][alloc_bucket_idx:] = [False] * (MVM_GEN2_PAGE_ITEMS - alloc_bucket_idx)
            elif page_idx > self.cur_page:
                alloc_bucket_idx = 0
                pagebuckets[page_idx] = [False] * MVM_GEN2_PAGE_ITEMS
            else:
                alloc_bucket_idx = MVM_GEN2_PAGE_ITEMS
            # sample a few objects for their size
            samplecount = int(min(MVM_GEN2_PAGE_CUBE_SIZE * EXTRA_SAMPLES, alloc_bucket_idx / 4))
            samples = sorted(random.sample(range(0, alloc_bucket_idx), samplecount))
            for idx in samples:
                stooge = (page_cursor.dereference() + (idx * self.bucket_size)).cast(gdb.lookup_type("MVMObjectStooge").pointer())
                sample_stooges.append((stooge, page_idx, idx))

            page_cursor += 1

        # now punch holes in our page_buckets
        free_cursor = self.g2sc['free_list']

        def address_to_page_and_bucket(addr):
            for idx, base in enumerate(page_addrs):
                end = base + self.bucket_size * MVM_GEN2_PAGE_ITEMS
                if base <= addr < end:
                    return idx, int((addr - base) / self.bucket_size)

        self.length_freelist = 0
        while free_cursor.cast(gdb.lookup_type("int")) != 0:
            if free_cursor.dereference().cast(gdb.lookup_type("int")) != 0:
                result = address_to_page_and_bucket(free_cursor.dereference())
                if result:
                    page, bucket = result
                    pagebuckets[page][bucket] = False
                    self.length_freelist += 1
            free_cursor = free_cursor.dereference().cast(gdb.lookup_type("char").pointer().pointer())
        print ""

        #doubles = defaultdict(lambda: 0)

        # now we can actually sample our objects
        for stooge, page, idx in sample_stooges:
            if pagebuckets[page][idx] != True:
                continue
            try:
                # XXX this really ought to get factored out
                size = int(stooge['common']['header']['size'])
                flags = stooge['common']['header']['flags']

                if size == 0:
                    print "found a null object!"
                    pagebuckets[page][idx] = None
                    continue

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

                #if REPRname == "P6num":
                    #data = stooge['data'].cast(gdb.lookup_type("MVMP6numBody"))
                    #doubles[str(float(data['value']))] += 1

                self.repr_histogram[REPRname] += 1
                self.size_histogram[size]     += 1
            except Exception as e:
                print e

        #if len(doubles) > 10:
            #show_histogram(doubles)

    def summarize(self):
        print "size bucket:", self.bucket_size
        if self.empty:
            print "(unallocated)"
            return
        print "setting up stuff"
        cols_per_block = int(math.sqrt(MVM_GEN2_PAGE_ITEMS))
        lines_per_block = cols_per_block / 2
        outlines = [[] for i in range(lines_per_block + 1)]
        break_step = PRETTY_WIDTH / (lines_per_block + 1)
        next_break = break_step
        pgct = 0
        fullpages = 0
        drawn = False
        for pgnum, page in enumerate(self.pagebuckets):
            if pgnum > self.cur_page:
                break
            if not all(page) and any(page):
                # hacky "take two at a time"
                for outline, (line, nextline) in enumerate(zip(*[iter(hilbert_coords)] * 2)):
                    for idx, (upper, lower) in enumerate(zip(line, nextline)):
                        upper, lower = page[upper], page[lower]
                        outlines[-lines_per_block + outline].append(shade_block(upper, lower))
                outlines[-lines_per_block - 1].append(str(str(pgnum + 1) + "st pg").center(cols_per_block) + " ")
                pgct += 1
                drawn = True
            else:
                fullpages += 1
                drawn = False

            if pgct > next_break:
                outlines.extend([[] for i in range(lines_per_block + 2)])
                next_break += break_step
            elif pgnum != self.cur_page and drawn:
                for line_num in range(lines_per_block):
                    outlines[-line_num - 1].append(" ")


        print (u"\n".join(map(lambda l: u"".join(l), outlines))).encode("utf8")
        if fullpages > 0:
            print "(and", fullpages, "completely filled pages)",
        if self.cur_page < len(self.pagebuckets):
            print "(and", (len(self.pagebuckets) - self.cur_page + 1), "empty pages)",
        if self.length_freelist > 0:
            print "(freelist with", self.length_freelist, "entries)",
        print ""

        # does the allocator/copier set the size of the object to the exact bucket size
        # automatically?
        if len(self.size_histogram) > 1:
            print "sizes of objects/stables:"
            try:
                show_histogram(self.size_histogram, "key", True)
            except Exception as e:
                print e
        if len(self.repr_histogram) >= 1:
            print "REPRs:"
            try:
                show_histogram(self.repr_histogram)
            except Exception as e:
                print e

class HeapData(object):
    run_nursery = None
    run_gen2    = None

    generation  = None

nursery_memory = []

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

        nursery_memory.append(nursery)

        #print "the current generation of the gc is", generation

        sizeclass_data = []
        for sizeclass in range(MVM_GEN2_BINS):
            g2sc = Gen2Data(generation, tc['gen2']['size_classes'][sizeclass], sizeclass)
            sizeclass_data.append(g2sc)
            g2sc.analyze(tc)

        for g2sc in sizeclass_data:
            g2sc.summarize()

        nursery.summarize()

class DiffHeapCommand(gdb.Command):
    def __init__(self):
        super(DiffHeapCommand, self).__init__("diff-moar-heap", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if arg != "":
            if " " in arg:
                pos1, pos2 = map(int, arg.split(" "))
            else:
                pos1, pos2 = int(arg), int(arg - 1)
        else:
            pos1 = -1
            pos2 = -2
        assert len(nursery_memory) > max(pos1, pos2)
        nursery_memory[pos2].diff(nursery_memory[pos1])

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
    commands.append(DiffHeapCommand())
    print "diff-moar-heap registered"

if __name__ == "__main__":
    register_printers(gdb.current_objfile())
    register_commands(gdb.current_objfile())
