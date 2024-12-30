# -*- coding: utf-8 -*-

# GDB will automatically load this module when you attach to the binary moar.
# but first you'll have to tell gdb that it's okay to load it. gdb will instruct
# you on how to do that.
# If it doesn't, you may need to copy or symlink this script right next to the
# moar binary in install/bin.
#
#     cd /path/to/install/bin
#     ln -s path/to/moarvm/tools/moar-gdb.py

# If you're developing/extending/changing this script, or if you're getting
# python exception messages, this command will be very helpful:
#
#    set python print-stack full

# This script contains a few helpers to make debugging MoarVM a bit more pleasant.
#
# So far, there's:
#
# - A semi-functional pretty-printer for MVMString and MVMString*
# - A non-working pretty-printer for MVMObject in general
# - A command "moar-heap" that walks the nursery and gen2 and displays
#   statistics about the REPRs found in them, as well as a display of
#   the fragmentation of the gen2 pages.
# - A command "diff-moar-heap" that diffs (so far only) the two last
#   snapshots of the nursery, or whatever snapshot number you supply
#   as the argument.

# Here's the TODO list:
#
# - Figure out if the string pretty-printer is hosed wrt. ropes or if
#   it's something wrong with MaarVM's ropes in general.
# - Implement diffing for the gen2 in some sensible manner
# - The backtrace should also display a backtrace of the interpreter
#   state. That's relatively easy, as you can just dump_backtrace(tc).
# - Give the object prety printer a children method that figures
#   stuff out about attributes of a P6opaque, or CStruct.
# - Let VMArray and MVMHash be displayed with the right display_hint
#   and also give them values for the children method
# - Pretty print P6bigint as their value
# - Pretty print P6int and P6num as their value

# Here's some wishlist items
#
# - Offer an HTML rendering of the stats, since gdb insists on printing
#   a pager header right in between our pretty gen2 graphs most of the time

import gdb
from collections import defaultdict
from itertools import chain
import math
import random
#import blessings
import sys
import time
import typing
import array
import struct
import pathlib
import tempfile

import json

import traceback # debugging

# These are the flags from MVMString's body.flags
str_t_info = {0: 'blob_32',
              1: 'blob_ascii',
              2: 'blob_8',
              3: 'strands',
              4: 'in_situ_8',
              5: 'in_situ_32'}

# How big to make the histograms and such
PRETTY_WIDTH=50

# This must be kept in sync with your MoarVM binary, otherwise we'll end up
# reading bogus data from the gen2 pages.
MVM_GEN2_PAGE_ITEMS = 256
MVM_GEN2_BIN_BITS   = 3
MVM_GEN2_BINS       = 40

# This ought to give the same results as the equivalent code in gen2.
def bucket_index_to_size(idx):
    return (idx + 1) << MVM_GEN2_BIN_BITS

# This is the size the gen2 pictures should have, just so we don't have to
# calculate the same sqrt of a constant over and over again.
MVM_GEN2_PAGE_CUBE_SIZE = int(math.sqrt(MVM_GEN2_PAGE_ITEMS))

# If you'd like more precision for the REPR histogram in the gen2, and have a
# bit of extra patience, turn this up.
EXTRA_SAMPLES = 2

# This corresponds to the defines in MVMArray.h for MVMArrayREPRData.slot_type.
array_storage_types = [
        'obj',
        'str',
        'i64', 'i32', 'i16', 'i8',
        'n64', 'n32',
        'u64', 'u32', 'u16', 'u8'
        ]

# These are used to display the hilbert curves extra-prettily.
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
    elif u is None and d is None:
        return halfblocks[4]
    elif u is None and d == False or u == False and d is None:
        return halfblocks[5]
    elif u is None and d == True or u == True and d is None:
        return halfblocks[6]


# Precalculate which index into the gen2 page goes at which coordinates in
# our super pretty hilbert curve.
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
            s = s // 2

        return d

    for y in range(n):
        hilbert_coords.append([])
        for x in range(n):
            hilbert_coords[-1].append(xy2d(x, y))

    return hilbert_coords

hilbert_coords = generate_hilbert(MVM_GEN2_PAGE_ITEMS)

# Sizes are easier to read if they have .s in them.
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
    """Whenever gdb encounters an MVMString or an MVMString*, this class gets
    instantiated and its to_string method tries its best to print out the
    actual contents of the MVMString's storage."""
    def __init__(self, val, pointer = False):
        self.val = val
        self.pointer = pointer

    def stringify(self):
        stringtyp = str_t_info[int(self.val['body']['storage_type']) & 0b11]
        if stringtyp in ("blob_32", "blob_ascii", "blob_8"):
            data = self.val['body']['storage'][stringtyp]
            pieces = []
            graphs = int(self.val['body']['num_graphs'])
            truncated = 0
            if graphs > 5000:
                truncated = graphs - 5000
                graphs = 5000
            for i in range(graphs):
                pdata = int((data + i).dereference())
                try:
                    # ugh, unicode woes ...
                    pieces.append(chr(pdata))
                except Exception:
                    pieces.append("\\x%x" % pdata)
            if truncated:
                pieces.append("... (truncated " + str(truncated) + " graphemes)")
            return "".join(pieces)
        elif stringtyp == "strands":
            # XXX here be dragons and/or wrong code
            # XXX This is still true now

            # i = 0
            # pieces = []
            # data = self.val['body']['storage']['strands']
            # end_reached = False
            # previous_index = 0
            # previous_string = None
            # while not end_reached:
                # strand_data = (data + i).dereference()
                # if strand_data['blob_string'] == 0:
                    # end_reached = True
                    # pieces.append(previous_string[1:-1])
                # else:
                    # the_string = strand_data['blob_string'].dereference()
                    # if previous_string is not None:
                        # pieces.append(
                            # str(previous_string)[1:-1][
                                # int(strand_data['start']) :
                                # int(strand_data['end']) - previous_index]
                            # )
                    # previous_string = str(the_string)
                    # previous_index = int(strand_data['end'])
                # i = i + 1
            # return "r(" + ")(".join(pieces) + ")"
            return None
        else:
            return "string of type " + stringtyp

    def to_string(self):
        result = self.stringify()
        if result:
            if self.pointer:
                return "pointer to '" + self.stringify() + "'"
            else:
                return "'" + self.stringify() + "'"
        else:
            return None

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

        debugname = as_mvmobject['st']['debug_name']

        return str(self.val.type.name) + " (" + debugname + ") of repr " + reprname

    def to_string(self):
        if self.pointer:
            return "pointer to " + self.stringify()
        else:
            return self.stringify()

def show_histogram(hist, sort="value", multiply=False):
    """In the context of this function, a histogram is a hash from an object
    that is to be counted to the number the object was found.

    sort takes "value" or "key" and gives you the ability to either sort
    by "order of buckets" or by "frequency of occurence".

    when giving multiply a value, you'll get a display of key * value on
    the right of the histogram, useful for a "size of object" to
    "count of objects" histogram so you'll get a sum of the taken space

    The histogram will not include values that are less than 2, because
    that may sometimes lead to "long tail" trouble and buttloads of
    pages of output to scroll through."""
    if len(hist) == 0:
        print("(empty histogram)")
        return
    if sort == "value":
        items = sorted(list(hist.items()), key = lambda vals: -vals[1])
    elif sort == "key":
        items = sorted(list(hist.items()), key = lambda vals: vals[0])
    else:
        print("sorting mode", sort, "not implemented")
    maximum = max(hist.values())
    keymax = min(max([len(str(key)) for key in hist.keys()]), 30)
    lines_so_far = 0
    group = -1
    num_in_group = 0
    for key, val in items:
        if lines_so_far < 50:
            try:
                str(key)
            except TypeError:
                key = repr(key)
            if val < 2:
                continue
            appendix = prettify_size(int(key) * int(val)).rjust(10) if multiply else ""
            print(str(key).ljust(keymax + 1), ("[" + "=" * int((float(hist[key]) / maximum) * PRETTY_WIDTH)).ljust(PRETTY_WIDTH + 1), str(val).ljust(len(str(maximum)) + 2), appendix)
        else:
            if val == group:
                num_in_group += 1
            else:
                if num_in_group > 1:
                    print(num_in_group, " x ", group)
                group = val
                num_in_group = 1
        lines_so_far += 1
    print()

def diff_histogram(hist_before, hist_after, sort="value", multiply=False):
    """Works almost exactly like show_histogram, but takes two histograms that
    should have matching keys and displays the difference both in graphical
    form and as numbers on the side."""
    max_hist = defaultdict(int)
    min_hist = defaultdict(int)
    zip_hist = {}
    max_val = 0
    max_key = 0
    longest_key = ""
    for k,v in chain(hist_before.items(), hist_after.items()):
        max_hist[k] = max(max_hist[k], v)
        min_hist[k] = min(max_hist[k], v)
        max_val = max(max_val, v)
        max_key = max(max_key, k)
        longest_key = str(k) if len(str(k)) > len(longest_key) else longest_key

    for k in max_hist.keys():
        zip_hist[k] = (hist_before[k], hist_after[k])

    if sort == "value":
        items = sorted(list(zip_hist.items()), key = lambda vals: -max(vals[1][0], vals[1][1]))
    elif sort == "key":
        items = sorted(list(zip_hist.items()), key = lambda vals: vals[0])
    else:
        print("sorting mode", sort, "not implemented")

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

        print(str(key).ljust(len(longest_key) + 2), bars.ljust(PRETTY_WIDTH), values, appendix)

class CommonHeapData(object):
    """This base class holds a bunch of histograms and stuff that are
    interesting regardless of wether we are looking at nursery objects
    or gen2 objects."""
    number_objects = None
    number_stables = None
    number_typeobs = None
    size_histogram = None
    repr_histogram = None
    opaq_histogram = None
    arrstr_hist    = None
    arrusg_hist    = None

    string_histogram = None

    generation     = None

    def __init__(self, generation):
        self.generation = generation

        self.size_histogram = defaultdict(int)
        self.repr_histogram = defaultdict(int)
        self.opaq_histogram = defaultdict(int)
        self.arrstr_hist    = defaultdict(int)
        self.arrusg_hist    = defaultdict(int)

        self.string_histogram = defaultdict(int)

        self.number_objects = 0
        self.number_stables = 0
        self.number_typeobs = 0

    def analyze_single_object(self, cursor):
        """Given a pointer into the nursery or gen2 that points at the
        beginning of a MVMObject of any sort, run a statistical analysis
        of what the object is and some extra info depending on its REPR.

        To make this scheme work well with the nursery analysis, it returns
        the size of the object analysed."""
        stooge = cursor.cast(gdb.lookup_type("MVMObjectStooge").pointer())
        size = int(stooge['common']['header']['size'])
        flags = int(stooge['common']['header']['flags'])

        is_typeobj = flags & 1
        is_stable = flags & 2

        STable = stooge['common']['st'].dereference()
        if not is_stable:
            REPR = STable["REPR"]
            REPRname = REPR["name"].string()
            try:
                debugname = STable['debug_name'].string()
            except gdb.MemoryError:
                debugname = "n/a"
            if is_typeobj:
                self.number_typeobs += 1
            else:
                self.number_objects += 1
        else:
            REPRname = "STable"
            debugname = "n/a"
            self.number_stables += 1

        self.size_histogram[int(size)] += 1
        if debugname != "n/a":
            self.repr_histogram[debugname] += 1
        else:
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
        elif REPRname == "MVMString":
            try:
                casted = cursor.cast(gdb.lookup_type('MVMString').pointer())
                stringresult = MVMStringPPrinter(casted).stringify()
                if stringresult is not None:
                    self.string_histogram[stringresult] += 1
                else:
                    self.string_histogram["mvmstr@" + hex(int(cursor.address.cast(gdb.lookup_type("int"))))] += 1
            except gdb.MemoryError as e:
                print(e)
                print(e.traceback())
                print(cursor.cast(gdb.lookup_type('MVMString').pointer()))

        return size

class NurseryData(CommonHeapData):
    """The Nursery Data contains the current position where we allocate as
    well as the beginning and end of the given nursery."""
    allocation_offs = None
    start_addr     = None
    end_addr       = None

    def __init__(self, generation, start_addr, end_addr, allocation_offs):
        super(NurseryData, self).__init__(generation)
        self.start_addr = gdb.Value(start_addr)
        self.end_addr = gdb.Value(end_addr)
        self.allocation_offs = allocation_offs

    def analyze(self, tc):
        cursor = gdb.Value(self.start_addr)
        info_step = int(self.allocation_offs - cursor) // 50
        next_info = cursor + info_step
        print("_" * 50)
        while cursor < self.allocation_offs:
            try:
                size = self.analyze_single_object(cursor)
            except Exception:
                print("while trying to analyze single object:");
                traceback.print_exc()
                print(stooge)
                print(stooge.__repr__())

            cursor += size
            if cursor > next_info:
                next_info += info_step
                sys.stdout.write("-")
                sys.stdout.flush()

        print()

    def summarize(self):
        print("nursery state:")
        sizes = (int(self.allocation_offs - self.start_addr), int(self.end_addr - self.allocation_offs))
        relsizes = [1.0 * size / (float(int(self.end_addr - self.start_addr))) for size in sizes]
        print("[" + "=" * int(relsizes[0] * 20) + " " * int(relsizes[1] * 20) + "] ", int(relsizes[0] * 100),"%")

        print(self.number_objects, "objects;", self.number_typeobs, " type objects;", self.number_stables, " STables")

        print("sizes of objects/stables:")
        show_histogram(self.size_histogram, "key", True)
        print("sizes of P6opaques only:")
        show_histogram(self.opaq_histogram, "key", True)
        print("debugnames:")
        show_histogram(self.repr_histogram)
        print("VMArray storage types:")
        show_histogram(self.arrstr_hist)
        print("VMArray usage percentages:")
        show_histogram(self.arrusg_hist, "key")

        print("strings:")
        show_histogram(self.string_histogram)

    def diff(self, other):
        print("nursery state --DIFF--:")

        print("sizes of objects/stables:")
        diff_histogram(self.size_histogram, other.size_histogram, "key", True)
        print("sizes of P6opaques only:")
        diff_histogram(self.opaq_histogram, other.opaq_histogram, "key", True)
        print("debugnames:")
        diff_histogram(self.repr_histogram, other.repr_histogram)
        print("VMArray storage types:")
        diff_histogram(self.arrstr_hist, other.arrstr_hist)
        print("VMArray usage percentages:")
        diff_histogram(self.arrusg_hist, other.arrusg_hist, "key")



class Gen2Data(CommonHeapData):
    """One Gen2Data instance gets created per size class.

    Thus, every instance corresponds to an exact size of object.

    The class also handles the free list that is chained through the
    gen2, so that fragmentation can be determined and stale objects not
    sampled for analysis."""
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
        self.repr_histogram = defaultdict(int)
        self.size_histogram = defaultdict(int)

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
        for page_idx in range(int(self.g2sc['num_pages'])):
            # collect the page addresses so that we can match arbitrary
            # pointers to page index/bucket index pairs later on.
            page_addrs.append(page_cursor.dereference())
            if page_idx == self.cur_page:
                # XXX cur_page is going to be removed from MoarVM, as it's only
                # XXX needed for this exact code here.

                # if the page we're looking at is the "current" page, we look
                # at the alloc_pos to find out where allocated objects stop.
                alloc_bucket_idx = int(int(self.g2sc['alloc_pos'] - page_cursor.dereference()) // self.bucket_size)
                pagebuckets[page_idx][alloc_bucket_idx:] = [False] * (MVM_GEN2_PAGE_ITEMS - alloc_bucket_idx)
            elif page_idx > self.cur_page:
                # if we're past the page we're currently allocating in, the
                # pages are empty by definition
                alloc_bucket_idx = 0
                pagebuckets[page_idx] = [False] * MVM_GEN2_PAGE_ITEMS
            else:
                # otherwise, the whole page is potentially allocated objects.
                alloc_bucket_idx = MVM_GEN2_PAGE_ITEMS

            # sample a few objects for later analysis (after free list checking)
            samplecount = int(min(MVM_GEN2_PAGE_CUBE_SIZE * EXTRA_SAMPLES, alloc_bucket_idx // 4))
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
                    return idx, int((addr - base) // self.bucket_size)

        self.length_freelist = 0
        while free_cursor.cast(gdb.lookup_type("int")) != 0:
            if free_cursor.dereference().cast(gdb.lookup_type("int")) != 0:
                result = address_to_page_and_bucket(free_cursor.dereference())
                if result:
                    page, bucket = result
                    pagebuckets[page][bucket] = False
                    self.length_freelist += 1
            free_cursor = free_cursor.dereference().cast(gdb.lookup_type("char").pointer().pointer())
        print("")

        #doubles = defaultdict(int)

        # now we can actually sample our objects
        for stooge, page, idx in sample_stooges:
            if pagebuckets[page][idx] != True:
                continue
            try:
                size = self.analyze_single_object(stooge)
            except Exception as e:
                print("while trying to analyze single object:");
                traceback.print_exc()
                print(stooge)
                print(stooge.__repr__())

        #if len(doubles) > 10:
            #show_histogram(doubles)

    def summarize(self):
        print("size bucket:", self.bucket_size)
        if self.empty:
            print("(unallocated)")
            return
        cols_per_block = int(math.sqrt(MVM_GEN2_PAGE_ITEMS))
        lines_per_block = cols_per_block // 2
        outlines = [[] for i in range(lines_per_block + 1)]
        break_step = PRETTY_WIDTH // (lines_per_block + 1)
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


        print((u"\n".join(map(lambda l: u"".join(l), outlines))))
        if fullpages > 0:
            print("(and", fullpages, "completely filled pages)",)
        if self.cur_page < len(self.pagebuckets):
            print("(and", (len(self.pagebuckets) - self.cur_page + 1), "empty pages)",)
        if self.length_freelist > 0:
            print("(freelist with", self.length_freelist, "entries)",)
        print("")

        # does the allocator/copier set the size of the object to the exact bucket size
        # automatically?
        if len(self.size_histogram) > 1:
            print("sizes of objects/stables:")
            try:
                show_histogram(self.size_histogram, "key", True)
            except Exception as e:
                print("while trying to show the size histogram...")
                print(e)
                print(e.traceback())
        if len(self.repr_histogram) >= 1:
            print("debugnames:")
            try:
                show_histogram(self.repr_histogram)
            except Exception as e:
                print("while trying to show the repr histogram...")
                print(e)
        print("strings:")
        show_histogram(self.string_histogram)

class OverflowData(CommonHeapData):
    def analyze(self, tc):
        g2a = tc['gen2']

        num_overflows = g2a["num_overflows"]
        print(num_overflows)

        try:
            for of_idx in range(num_overflows):
                of_obj = g2a["overflows"][of_idx]
                self.analyze_single_object(of_obj)
        except Exception:
            print("error while analyze_single_object or something");

    def summarize(self):
        print("overflows in the gen2")

        print(self.number_objects, "objects;", self.number_typeobs, " type objects;", self.number_stables, " STables")

        print("sizes of objects/stables:")
        show_histogram(self.size_histogram, "key", True)
        print("sizes of P6opaques only:")
        show_histogram(self.opaq_histogram, "key", True)
        print("debugnames:")
        show_histogram(self.repr_histogram)
        print("VMArray storage types:")
        show_histogram(self.arrstr_hist)
        print("VMArray usage percentages:")
        show_histogram(self.arrusg_hist, "key")

        print("strings:")
        show_histogram(self.string_histogram)


class HeapData(object):
    run_nursery = None
    run_gen2    = None

    generation  = None

nursery_memory = []

class AnalyzeHeapCommand(gdb.Command):
    """Analyze the nursery and gen2 of MoarVM's garbage collector corresponding
    to the current tc, or the tc you pass as the first argument"""
    def __init__(self):
        super(AnalyzeHeapCommand, self).__init__("moar-heap", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        tc = gdb.selected_frame().read_var(arg if arg else "tc")
        if not str(tc.type).startswith("MVMThreadContext"):
            raise ValueError("Please invoke the heap analyzer command on a MVMThreadContext, usually tc.")

        try:
            # find out the GC generation we're in (just a number increasing by 1 every time we GC)
            instance = tc['instance']
            generation = instance['gc_seq_number']

            nursery = NurseryData(generation, tc['nursery_tospace'], tc['nursery_alloc_limit'], tc['nursery_alloc'])

            nursery.analyze(tc)

            nursery_memory.append(nursery)

            print("the current generation of the gc is", generation)

            sizeclass_data = []
            for sizeclass in range(MVM_GEN2_BINS):
                g2sc = Gen2Data(generation, tc['gen2']['size_classes'][sizeclass], sizeclass)
                sizeclass_data.append(g2sc)
                g2sc.analyze(tc)

            overflowdata = OverflowData(generation)

            overflowdata.analyze(tc)

            for g2sc in sizeclass_data:
                g2sc.summarize()

            nursery.summarize()

            overflowdata.summarize()
        except KeyboardInterrupt:
            print("aborted the analysis.")

class DiffHeapCommand(gdb.Command):
    """Display the difference between two snapshots of the nursery."""
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

class AutoResumingBreakpoint(gdb.Breakpoint):
    def stop(self):
        self._hit_breakpoint_action()
        return False # don't actually stop

class PointInTimeRecordingBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, event, exprs, *args):
        super(PointInTimeRecordingBreakpoint, self).__init__(*args)
        self._medbc = medbc # MakeExecutionDatabaseCommand
        self._event = event
        self._exprs = exprs
        self._extras = None

    def _hit_breakpoint_action(self):
        created_event = self._medbc._register_event(self._event, self._exprs)
        if self._extras is not None:
            self._extras(created_event)

    def _extra(self, _the_extra):
        self._extras = _the_extra
        return self


class ArrayEntryRecordingBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, table_name, exprs, *args):
        super(ArrayEntryRecordingBreakpoint, self).__init__(*args)
        self._medbc = medbc # MakeExecutionDatabaseCommand
        self._table_name = table_name
        self._exprs = exprs
        self._extras = None

    def _hit_breakpoint_action(self):
        created_event = self._medbc._register_event_array(self._table_name, self._exprs)
        if self._extras is not None:
            self._extras(created_event)

    def _extra(self, _the_extra):
        self._extras = _the_extra
        return self


class ObjectMovementRecordingBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, *args):
        super(ObjectMovementRecordingBreakpoint, self).__init__(*args)
        self._medbc = medbc

    def _hit_breakpoint_action(self):
        self._medbc._register_object_movement()

execution_db = None

class MakeExecutionDatabaseCommand(gdb.Command):
    """Run execution from beginning to end, creating a database with interesting events.

    Only makes sense to run inside a "rr replay" session."""
    def __init__(self):
        super(MakeExecutionDatabaseCommand, self).__init__("moar-rrdb", gdb.COMMAND_DATA)

    _disabled_breakpoints = None
    _created_breakpoints = None

    _gc_seq_num = None

    _db = None
    _object_movements = {}

    class _EXP:
        tid = lambda _: ["tid", gdb.selected_thread().ptid_string]
        gdb_eval = lambda _, sub, code, pytype: lambda: [sub, pytype(gdb.parse_and_eval(code))]

    _prev_tl_entry = None
    _prev_thread = None
    _prev_thread_str = None

    def _register_event(self, event, exprs):
        rr_event = int(gdb.execute("when", False, True).replace("Completed event: ", "").replace("\n", ""))
        rr_tick  = int(gdb.execute("when-ticks", False, True).replace("Current tick: ", "").replace("\n", ""))
        rr_pos = str(rr_tick)
        expr_data = dict(rre=rr_event, rrt=rr_tick, event=event)
        for expr in exprs:
            if isinstance(expr, list):
                key, value = expr
            elif isinstance(expr, typing.Callable):
                key, value = expr()
            expr_data[key] = value

        before_del = expr_data.copy()

        if expr_data["subject"]:
            sk_data = self._db["subjects"][expr_data["subject_kind"]]
            if expr_data["subject"] not in sk_data:
                sk_data[expr_data["subject"]] = subjdata = dict(events=[])
            else:
                subjdata = sk_data[expr_data["subject"]]
            subjdata["events"].append(before_del)

        #for k in self._prev_tl_entry:
        #    if k in expr_data and self._prev_tl_entry[k] == expr_data[k]:
        #        del expr_data[k]

        self._db["timeline"][rr_pos] = expr_data
        self._prev_tl_entry = before_del

        return before_del

    def _register_event_array(self, table_name, exprs):
        # rr_event = int(gdb.execute("when", False, True).replace("Completed event: ", "").replace("\n", ""))
        rr_tick  = int(gdb.execute("when-ticks", False, True).replace("Current tick: ", "").replace("\n", ""))

        table = self._db['subjects'][table_name]

        for expr in exprs:
            if isinstance(expr, list):
                key, value = expr
            elif isinstance(expr, typing.Callable):
                key, value = expr()
            table[key].append(value)
        table["rr_tick"].append(rr_tick)

        return None

    def _register_object_movement(self):
        t = gdb.selected_thread()
        if t is self._prev_thread:
            tid = self._prev_thread_str
        else:
            tid = t.ptid_string
            self._prev_thread = t
            self._prev_thread_str = tid

        to_addr   = int(gdb.parse_and_eval("(uintptr_t)new_addr"))
        from_addr = int(gdb.parse_and_eval("(uintptr_t)item"))

        self._object_movements[self._gc_seq_num].append(from_addr)
        self._object_movements[self._gc_seq_num].append(to_addr)

    def setup(self):
        def store_seq_num(ev):
            prev = self._gc_seq_num
            self._gc_seq_num = int(gdb.parse_and_eval("tc->instance->gc_seq_number"))

            if self._gc_seq_num not in self._object_movements:
                self._object_movements[self._gc_seq_num] = array.array("Q")

            if prev is not None and prev // 20 != self._gc_seq_num // 20:
                print(time.strftime("%H:%M:%S"), " - reached gc run ", self._gc_seq_num)
                for s in self._db["subjects"]:
                    print("            - ", s, " has ", len(list(self._db["subjects"][s].items())[0][1]), " entries")
                if self._gc_seq_num == 60:
                    gdb.Breakpoint("MVM_frame_dispatch")

        print(time.strftime("%H:%M:%S"), " - starting")

        #sfs_schema = pa.schema([
        #    ('rr_tick', pa.uint64()),
        #    ('addr', pa.uint64()),
        #    ('compunit', pa.uint64()),
        #    ('bytecode_addr', pa.uint64()),
        #    ('event', pa.string()),
        #    ])

        #cus_schema = pa.schema([
        #    ('rr_event', pa.uint64()),
        #    ('addr', pa.uint64()),
        #    ('mapped_addr', pa.uint64()),
        #    ('event', pa.string()),
        #    ])

        #moves_schema = pa.schema([
        #    ('gc_seq_nr', pa.uint32()),
        #    ('pre_addr', pa.uint64()),
        #    ('post_addr', pa.uint64()),
        #    ])

        #calls_schema = pa.schema([
        #    ('rr_tick', pa.uint64()),
        #    ('sf_addr', pa.uint64()),
        #    ('bytecode_addr', pa.uint64()),
        #    ('tc_addr', pa.uint64()),
        #])

        cus_tbl = {
            "tc": array.array('Q'),
            "rr_tick": array.array('Q'),
            "data_addr": array.array('Q'),
        }

        sfs_tbl = {
            "rr_tick": array.array('Q'),
            "bytecode": array.array('Q'),
            "compunit": array.array('Q'),
            "sf": array.array('Q'),
            "cuuid": [],
            "name": [],
        }

        spesh_bytecode_tbl = {
            "rr_tick": array.array('Q'),
            "sf": array.array('Q'),
            "bytecode": array.array('Q'),
            "size": array.array('L'),
        }

        calls_tbl = {
            "rr_tick": array.array('Q'),
            "bytecode_addr": array.array('Q'),
            "frame": array.array('Q'),
        }

        gcs_tbl = {
            "tc": array.array('Q'),
            "rr_tick": array.array('Q'),
            "gc_seq_number": array.array('L'),
            "is_full": array.array('B'),
        }

        worklist_runs_tbl = {
            "tc": array.array('Q'),
            "rr_tick": array.array('Q'),
            "nursery_alloc": array.array('Q'),
            "nursery_alloc_limit": array.array('Q'),
            "nursery_fromspace": array.array('Q'),
            "nursery_tospace": array.array('Q'),
        }

        print(gcs_tbl)

        EXP = self._EXP()
        self._db = dict(
            timeline={},
            subjects=dict(
                gcs=gcs_tbl,
                sfs=sfs_tbl,
                spesh_bytecode=spesh_bytecode_tbl,
                calls=calls_tbl,
                cus=cus_tbl,
                worklist_runs=worklist_runs_tbl,
            ),
            object_movements={},
        )
        self._prev_tl_entry = {}
        self._object_movements = self._db["object_movements"]

        execution_db = self._db

        self._disabled_breakpoints = []
        self._created_breakpoints = []
        cbp = self._created_breakpoints

        for bp in gdb.breakpoints():
            if bp.enabled:
                bp.enabled = False
                self._disabled_breakpoints.append(bp)

        cbp.append(ArrayEntryRecordingBreakpoint(self, "gcs", [
            EXP.gdb_eval("tc", "tc", int),
            EXP.gdb_eval("gc_seq_number", "tc->instance->gc_seq_number", int),
            EXP.gdb_eval("is_full", "tc->instance->gc_full_collect", int),
            ], "run_gc"))

        cbp.append(ArrayEntryRecordingBreakpoint(self, "cus", [
            EXP.gdb_eval("tc", "tc", int),
            EXP.gdb_eval("data_addr", "cu->body.data_start", int),
            ], "run_comp_unit"))

        cbp.append(ArrayEntryRecordingBreakpoint(self, "worklist_runs", [
            EXP.gdb_eval("tc", "tc", int),
            EXP.gdb_eval("nursery_alloc", "tc->nursery_alloc", int),
            EXP.gdb_eval("nursery_alloc_limit", "tc->nursery_alloc_limit", int),
            EXP.gdb_eval("nursery_fromspace", "tc->nursery_fromspace", int),
            EXP.gdb_eval("nursery_tospace", "tc->nursery_tospace", int),
            ], "process_worklist")._extra(store_seq_num))

        gdb.execute("list process_worklist", False, True)
        forwarder_update_lineno = gdb.execute("search item->sc_forward_u.forwarder = new_addr;", False, True).split("\t")[0]

        cbp.append(ObjectMovementRecordingBreakpoint(self, "src/gc/collect.c:" + forwarder_update_lineno))

        gdb.execute("list MVM_frame_dispatch", False, True)
        dispatch_trampoline_lineno = gdb.execute("search MVM_jit_code_trampoline(tc);", False, True).split("\t")[0]

        #cbp.append(ArrayEntryRecordingBreakpoint(self, "calls", [
        #    #EXP.gdb_eval("tc", "tc", int),
        #    EXP.gdb_eval("bytecode_addr", "chosen_bytecode", int),
        #    EXP.gdb_eval("frame", "frame", int),
        #    ], "src/core/frame.c:" + dispatch_trampoline_lineno))

        print("trying to find a line in MVM_frame_move_to_heap")
        gdb.execute("list MVM_frame_move_to_heap", False, True)
        get_to_promote_sf_lineno = gdb.execute("search MVMStaticFrame *sf = cur_to_promote->static_info;", False, True).split("\t")[0]
        print("oh well")

        gdb.execute("list MVM_spesh_candidate_add", False, True)
        free_speshcode_lineno = gdb.execute("search MVM_free.sc.;", False, True).split("\t")[0]

        cbp.append(ArrayEntryRecordingBreakpoint(self, "spesh_bytecode", [
            EXP.gdb_eval("sf", "p->sf", int),
            EXP.gdb_eval("bytecode", "sc->bytecode", int),
            EXP.gdb_eval("size", "sc->bytecode_size", int),
            ], "src/6model/reprs/MVMSpeshCandidate.c:" + free_speshcode_lineno))

        cbp.append(ArrayEntryRecordingBreakpoint(self, "sfs", [
            EXP.gdb_eval("bytecode", "static_frame->body.bytecode", int),
            EXP.gdb_eval("compunit", "static_frame->body.cu", int),
            EXP.gdb_eval("sf", "static_frame", int),
            EXP.gdb_eval("cuuid", "MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid)", lambda v: v.string()),
            EXP.gdb_eval("name", "MVM_string_utf8_encode_C_string(tc, static_frame->body.name)", lambda v: v.string()),
            ], "MVM_validate_static_frame"))


        # sf->body.fully_deserialized = 1;

        #cbp.append(PointInTimeRecordingBreakpoint(self, "force_frame_to_heap", [
        #    EXP.tid,
        #    ["subject_kind", "frames"],
        #    EXP.gdb_eval("subject", "cur_to_promote->static_info", int),
        #    EXP.gdb_eval("frame", "cur_to_promote", int),
        #    EXP.gdb_eval("frame_new", "promoted", int),
        #    ], "src/core/frame.c:" + get_to_promote_sf_lineno))

        #cbp.append(ArrayEntryRecordingBreakpoint(self, "calls", [
        #    EXP.tid,
        #    ["subject_kind", "frames"],
        #    EXP.gdb_eval("subject", "returner", int),
        #    ], "callstack.c:exit_frame"))

    def teardown(self):
        for bp in self._disabled_breakpoints:
            bp.enabled = True
        for bp in self._created_breakpoints:
            bp.delete()


    def run(self):
        gdb.execute("start")
        # run to the temporary breakpoint that "start" creates
        gdb.execute("c")
        # run the program actually
        gdb.execute("c")

    def invoke(self, arg, from_tty):
        self.setup()
        try:
            self.run()
        finally:
            self.teardown()

        with tempfile.NamedTemporaryFile(mode="wb", prefix="moar_gdb_", suffix="_rrdb.pkl", delete=False) as ntf:
            import pickle
            pickle.dump(self._db, ntf)
            ntf.close()

            print("rrdb pickle file can be found at ", ntf.name)

def str_lookup_function(val):
    if str(val.type) == "MVMString":
        return MVMStringPPrinter(val)
    elif str(val.type) == "MVMString *":
        return MVMStringPPrinter(val, True)

    return None

def mvmobject_lookup_function(val):
    pointer = str(val.type).endswith("*")
    if str(val.type).startswith("MVM"):
        try:
            val.cast(gdb.lookup_type("MVMObject" + (" *" if pointer else "")))
            return MVMObjectPPrinter(val, pointer)
        except Exception as e:
            print("couldn't cast this:", e)
    return None

def register_printers(objfile):
    objfile.pretty_printers.append(str_lookup_function)
    print("MoarVM string pretty printer registered")
    # XXX since this is currently nonfunctional, just ignore it for now
    # objfile.pretty_printers.append(mvmobject_lookup_function)
    # print("MoarVM Object pretty printer registered")

commands = []
def register_commands(objfile):
    commands.append(AnalyzeHeapCommand())
    print("moar-heap registered")
    commands.append(DiffHeapCommand())
    print("diff-moar-heap registered")
    commands.append(MakeExecutionDatabaseCommand())
    print("moar-rrdb registered")

# We have to introduce our classes to gdb so that they can be used
if __name__ == "__main__":
    the_objfile = gdb.current_objfile()
    if the_objfile is None:
        try:
            the_objfile = gdb.lookup_objfile("libmoar.so")
        except:
            print("GDB doesn't know about 'libmoar.so' yet; maybe you need to run the program a little until it's loaded.")
    if the_objfile:
        register_printers(the_objfile)
    register_commands(the_objfile)
