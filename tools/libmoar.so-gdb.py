# -*- coding: utf-8 -*-

# GDB will automatically load this module when you attach to the binary moar.
# but first you'll have to tell gdb that it's okay to load it. gdb will instruct
# you on how to do that.
# The MoarVM Makefile puts libmoar.so-gdb.py next to the libmoar.so file, which
# should cause GDB to find it and tell you if you need a setting to make it
# actually activate.

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
# - A command "moar heap" that walks the nursery and gen2 and displays
#   statistics about the REPRs found in them, as well as a display of
#   the fragmentation of the gen2 pages.
# - A command "moar diff-heap" that diffs (so far only) the two last
#   snapshots of the nursery, or whatever snapshot number you supply
#   as the argument.
# - A group of commands "moar break" for commonly useful breakpoints.
# - A command "moar bt" that shows a backtrace including each frame's arguments.

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

from dataclasses import dataclass
from pathlib import Path
from collections.abc import Mapping, Sequence, Set
from enum import Enum
import gdb
from collections import defaultdict
from itertools import chain
import math
import random
import sys
import time
import typing
import tempfile

import gdb.printing

import traceback # debugging

uint32_t : gdb.Type | None
uint32p_t : gdb.Type | None

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

# Declaration of the "moar" command group.
class MoarCommands(gdb.Command):
    """Group of commands related to MoarVM."""
    def __init__(self):
        super(MoarCommands, self).__init__("moar", gdb.COMMAND_NONE, prefix=True)


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

#                  _   _                   _     _
#     _ __ _ _ ___| |_| |_ _  _   _ __ _ _(_)_ _| |_ ___ _ _ ___
#    | '_ \ '_/ -_)  _|  _| || | | '_ \ '_| | ' \  _/ -_) '_(_-<
#    | .__/_| \___|\__|\__|\_, | | .__/_| |_|_||_\__\___|_| /__/
#    |_|                   |__/  |_|

repr_infos = dict()

def _first_found_type(candidates):
    for c in candidates:
        try:
            return (c, gdb.lookup_type(c))
        except gdb.error:
            pass
    raise ValueError(f"None of the candidates {candidates} could be found as a type by gdb")

def init_repr_types_and_structs():
    c_r_id = 0
    repr_errors = []
    def _ri(name : str, structname = None, reprdatastruct = None, repridname = None):
        nonlocal c_r_id
        try:
            structnames = [name]
            if structname is not None:
                structnames.append(structname)
            if not name.startswith("MVM"):
                structnames.append("MVM" + name)
            if not name.startswith("P6"):
                structnames.append("MVM" + name)

            structname, structtype = _first_found_type(structnames)

            info = {
                "name": name,
                "reprid": c_r_id,
                "struct": structtype,
                "reprops": None, # TODO how best to find all the reprops structs?
            }
            if reprdatastruct is None:
                reprdatastruct = f"{structname}REPRData"
            if reprdatastruct is not False:
                try:
                    info["reprdatastruct"] = gdb.lookup_type(reprdatastruct)
                except gdb.error:
                    info["reprdatastruct"] = None
            repr_infos[name] = info
        except:
            repr_errors.append(name)
        c_r_id += 1

    _ri("MVMString")
    _ri("VMArray", "MVMArray", "MVMArrayREPRData")
    _ri("MVMHash")
    _ri("MVMCFunction")
    _ri("KnowHOWREPR")
    _ri("P6opaque")
    _ri("MVMCode")
    _ri("MVMOSHandle")
    _ri("P6int")
    _ri("P6num")
    _ri("Uninstantiable")
    _ri("HashAttrStore")
    _ri("KnowHOWAttributeREPR")
    _ri("P6str")
    _ri("MVMThread")
    _ri("MVMIter")
    _ri("MVMContext")
    _ri("SCRef")
    _ri("MVMSpeshLog")
    _ri("P6bigint")
    _ri("NFA")
    _ri("MVMException")
    _ri("MVMStaticFrame")
    _ri("MVMCompUnit")
    _ri("MVMDLLSym")
    _ri("MVMContinuation")
    _ri("MVMNativeCall")
    _ri("MVMCPointer")
    _ri("MVMCStr")
    _ri("MVMCArray")
    _ri("MVMCStruct")
    _ri("ReentrantMutex")
    _ri("ConditionVariable")
    _ri("Semaphore")
    _ri("ConcBlockingQueue")
    _ri("MVMAsyncTask")
    _ri("MVMNull")
    _ri("NativeRef")
    _ri("MVMCUnion")
    _ri("MultiDimArray")
    _ri("MVMCPPStruct")
    _ri("Decoder")
    _ri("MVMStaticFrameSpesh")
    _ri("MVMSpeshCandidate")
    _ri("MVMCapture")
    _ri("MVMTracked")
    _ri("MVMStat")

    print(f"Registered {len(repr_infos)} REPRs.")
    if len(repr_errors):
        print(f"... could not register these: {repr_errors}")

def mvmstr_to_str(val, start=0, strlen=None, truncate=5000):
    stringtyp = str_t_info[int(val['body']['storage_type'])]
    if stringtyp in ("blob_32", "blob_ascii", "blob_8", "in_situ_8", "in_situ_32"):
        data = val['body']['storage'][stringtyp]
        pieces = []
        graphs = int(val['body']['num_graphs'])
        truncated = 0

        graphs = graphs - start
        if strlen is not None and strlen > graphs:
            graphs = strlen

        if graphs > truncate :
            truncated = graphs - truncate
            graphs = truncate

        for i in range(graphs):
            pdata = int(data[i])
            if pdata < 0:
                # XXX synthetics currently not supported
                pieces.append("\\s{-%x}" % (-pdata))
            else:
                try:
                    # ugh, unicode woes ...
                    pieces.append(chr(pdata))
                except Exception:
                    pieces.append("\\x%x" % pdata)
        if truncated:
            pieces.append("... (truncated " + str(truncated) + " graphemes)")
        return "".join(pieces)
    elif stringtyp == "strands":
        data = val['body']['storage'][stringtyp]
        pieces = []
        for p in range(val['body']['num_strands']):
            # XXX probably a good idea to test thoroughly and see that nothing is off-by-one etc
            strand = data[p]
            graphs_one = int(strand['end']) - int(strand['start'])
            graphs_all = graphs_one
            if int(strand['repetitions']) > 0:
                graphs_all = graphs_one * (int(strand['repetitions']) + 1)


            # current strand is completely before the spot we're starting at
            if graphs_all < start:
                start -= graphs_all
                continue

            str_one = mvmstr_to_str(strand['blob_string'])
            str_full = str_one * (int(strand['repetitions']) + 1)

            pieces.append(str_full[start:])
            start = 0
            if strlen is not None:
                if strlen < len(pieces[-1]):
                    pieces[-1] = pieces[-1][:strlen]
                    break
                strlen -= len(pieces[-1])

        output = "".join(pieces)

        if len(output) > truncate:
            return output[:truncate] + "... (truncated " + str(len(output) - truncate) + " graphemes)"
        else:
            return output

class MVMStringPPrinter(gdb.printing.PrettyPrinter):
    """Whenever gdb encounters an MVMString or an MVMString*, this class gets
    instantiated and its to_string method tries its best to print out the
    actual contents of the MVMString's storage."""
    def __init__(self, val, pointer = False):
        self.val = val
        self.pointer = pointer

    def to_string(self):
        if self.pointer:
            return "(MVMString *)'" + mvmstr_to_str(self.val) + "'"
        else:
            return "(MVMString)'" + mvmstr_to_str(self.val) + "'"

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

#                                                _           _
#      _ __  ___ _ __  ___ _ _ _  _   __ ___ _ _| |_ ___ _ _| |_
#     | '  \/ -_) '  \/ _ \ '_| || | / _/ _ \ ' \  _/ -_) ' \  _|
#     |_|_|_\___|_|_|_\___/_|  \_, | \__\___/_||_\__\___|_||_\__|
#                              |__/
#                     _         _
#      __ _ _ _  __ _| |_  _ __(_)___
#     / _` | ' \/ _` | | || (_-< (_-<
#     \__,_|_||_\__,_|_|\_, /__/_/__/
#                       |__/
#

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

#                                                                    _
#     _ __  ___ _ __  ___ _ _ _  _   __ ___ _ __  _ __  __ _ _ _  __| |___
#    | '  \/ -_) '  \/ _ \ '_| || | / _/ _ \ '  \| '  \/ _` | ' \/ _` (_-<
#    |_|_|_\___|_|_|_\___/_|  \_, | \__\___/_|_|_|_|_|_\__,_|_||_\__,_/__/
#                             |__/

class AnalyzeHeapCommand(gdb.Command):
    """Analyze the nursery and gen2 of MoarVM's garbage collector corresponding
    to the current tc, or the tc you pass as the first argument"""
    def __init__(self):
        super(AnalyzeHeapCommand, self).__init__("moar heap", gdb.COMMAND_DATA)

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
        super(DiffHeapCommand, self).__init__("moar diff-heap", gdb.COMMAND_DATA)

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

#                         _   _                                            _
#     _____ _____ __ _  _| |_(_)___ _ _    __ ___ _ __  _ __  __ _ _ _  __| |___
#    / -_) \ / -_) _| || |  _| / _ \ ' \  / _/ _ \ '  \| '  \/ _` | ' \/ _` (_-<
#    \___/_\_\___\__|\_,_|\__|_\___/_||_| \__\___/_|_|_|_|_|_\__,_|_||_\__,_/__/
#


class MoarBreakCommands(gdb.Command):
    """Group of commands to set breakpoints at useful MoarVM functions."""
    _break_spec = ""
    def __init__(self):
        super(MoarBreakCommands, self).__init__("moar break", gdb.COMMAND_BREAKPOINTS, prefix=True)

    def invoke(self, argument, from_tty):
        return gdb.Breakpoint(self._break_spec)

class MoarBreakInterpRunCommands(MoarBreakCommands):
    """Set a breakpoint at MVM_interp_run."""
    def __init__(self):
        super(MoarBreakCommands, self).__init__("moar break interp-run", gdb.COMMAND_BREAKPOINTS)
        self._break_spec = "MVM_interp_run"

class MoarBreakExceptionAdhoc(MoarBreakCommands):
    """Set a breakpoint at MVM_exception_throw_adhoc."""
    def __init__(self):
        super(MoarBreakCommands, self).__init__("moar break exception-adhoc", gdb.COMMAND_BREAKPOINTS)
        self._break_spec = "MVM_exception_throw_adhoc"

#     __  __             __   ____  __   ___ ___ ___  ___
#    |  \/  |___  __ _ _ \ \ / /  \/  | | _ \ _ \   \| _ )
#    | |\/| / _ \/ _` | '_\ V /| |\/| | |   /   / |) | _ \
#    |_|  |_\___/\__,_|_|  \_/ |_|  |_| |_|_\_|_\___/|___/
#
#
# when running moar under `rr replay` we can make a little database of
# interestang events, as well as information about objects by their address
# which could then be used by different commands.
#
# The database is currently created as an sqlite3 database which is filled
# with data from breakpoints automatically set up at "strategic places".
#
# As of right now, the data is not utilised by any commands here.
#

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

class SQLRecordingBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, table_name : str, column_names : str, exprs : Sequence, *args):
        super(SQLRecordingBreakpoint, self).__init__(*args)
        self._medbc = medbc # MakeExecutionDatabaseCommand
        self._table_name = table_name
        self._exprs = exprs
        self._extras = None
        self._column_expr = ', '.join([":" + n for n in column_names.split(" ")])

    def _hit_breakpoint_action(self):
        created_event = self._medbc._register_event_sql(self._table_name, self._column_expr, self._exprs)
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

def follow_fields(val : gdb.Value, fields : list[str]):
    for step in fields:
        val = val[step]
    return val

class MakeExecutionDatabaseCommand(gdb.Command):
    """Run execution from beginning to end, creating a database with interesting events.

    Only makes sense to run inside a "rr replay" session.

    Possible arguments:

      - notrackgc      Don't record every object's old and new addresses when doing GC
    """

    flags : Set[str]

    def __init__(self):
        super(MakeExecutionDatabaseCommand, self).__init__("moar rrdb", gdb.COMMAND_DATA)

    _disabled_breakpoints = None
    _created_breakpoints = None

    _gc_seq_num = None

    class _EXP:
        tid         = lambda _: lambda _: ["tid", gdb.selected_thread().ptid_string]
        gdb_eval    = lambda _, sub, code, pytype: lambda _: [sub, pytype(gdb.parse_and_eval(code))]
        local_var   = lambda _, sub, var_name, pytype: lambda frame: [sub, pytype(frame.read_var(var_name))]
        local_field = lambda _, sub, var_name, steps, pytype: lambda frame: [sub, pytype(follow_fields(frame.read_var(var_name), steps))]

        def local_fields(cls, var_name, steps, fields, pytype):
            def local_fields_impl(frame):
                val = frame.read_var(var_name)
                for step in steps:
                    val = val[step]
                res_keys = []
                res_vals = []
                for (colname, fieldname) in fields:
                    res_keys.append(colname)
                    res_vals.append(pytype(val[fieldname]))
                return res_keys, res_vals
            return local_fields_impl

    _prev_tl_entry = None
    _prev_thread = None
    _prev_thread_str = None

    _last_saved_event_time = -1

    def _register_event_sql(self, table_name, column_expr, exprs):
        conn = gdb.connections()[0]
        assert isinstance(conn, gdb.RemoteTargetConnection)

        currthreadptid = conn.send_packet("qC")
        currthreadptid = int(currthreadptid[currthreadptid.rindex(b".") + 1:], 16)

        resp     = bytes.fromhex(conn.send_packet('qRRCmd:when:' + str(currthreadptid)))
        rr_event = int(resp[resp.rindex(b' '):])

        resp     = bytes.fromhex(conn.send_packet('qRRCmd:when-ticks:' + str(currthreadptid)))
        rr_tick  = int(resp[resp.rindex(b' '):])

        frame = gdb.selected_frame()

        # rr_event = int(gdb.execute("when", False, True).replace("Completed event: ", "").replace("\n", ""))
        # rr_tick = 0
        # rr_tick  = int(gdb.execute("when-ticks", False, True).replace("Current tick: ", "").replace("\n", ""))

        row = {}
        for expr in exprs:
            try:
                if isinstance(expr, list):
                    key, value = expr
                elif isinstance(expr, typing.Callable):
                    key, value = expr(frame)

                if isinstance(key, str):
                    row[key] = value
                else:
                    for i in range(len(key)):
                        row[key[i]] = value[i]
            except Exception:
                print("when evaluating expr ", expr, " for table ", table_name)
                raise

        row["rr_tick"] = rr_tick
        row["rr_event"] = rr_event

        query = f"INSERT INTO {table_name} VALUES ({column_expr});"
        self._db_cur.execute(query, row)

        if rr_event != self._last_saved_event_time:
            resp     = bytes.fromhex(conn.send_packet('qRRCmd:elapsed-time:' + str(currthreadptid)))
            rr_time = float(resp[resp.rindex(b' ') + 1:])

            query = f'INSERT INTO event_times VALUES (?, ?)';
            self._db_cur.execute(query, (rr_event, rr_time))
            self._last_saved_event_time = rr_event

        self._db_conn.commit()

        # helpers for "extras" handlers
        row["__frame"] = frame
        row["__currthreadptid"] = currthreadptid

        return row

    _last_saved_movement_event = -1

    def _register_object_movement(self):
        to_addr   = int(gdb.parse_and_eval("(uintptr_t)new_addr"))
        from_addr = int(gdb.parse_and_eval("(uintptr_t)item"))
        rr_event = int(gdb.execute("when", False, True).replace("Completed event: ", "").replace("\n", ""))

        if self._last_saved_movement_event != rr_event:
            query = "INSERT INTO object_movements VALUES (?, ?, ?);"
            self._db_cur.execute(query, (rr_event, from_addr, to_addr))
            self._db_conn.commit()
            self._last_saved_movement_event = rr_event
        else:
            query = "INSERT INTO object_movements VALUES (null, ?, ?);"
            self._db_cur.execute(query, (from_addr, to_addr))
            self._db_conn.commit()


    def setup(self):
        import sqlite3

        self._last_saved_event_time = -1

        def store_seq_num(ev):
            prev = self._gc_seq_num
            # self._gc_seq_num = int(gdb.parse_and_eval("tc->instance->gc_seq_number"))
            self._gc_seq_num = int(ev["__frame"].read_var("tc")["instance"]["gc_seq_number"])

            #if self._gc_seq_num not in self._object_movements:
            #    self._object_movements[self._gc_seq_num] = array.array("Q")

            if prev is not None and prev // 20 != self._gc_seq_num // 20:
                rr_event = ev["rr_event"]

                conn = gdb.connections()[0]
                assert isinstance(conn, gdb.RemoteTargetConnection)

                resp     = bytes.fromhex(conn.send_packet('qRRCmd:elapsed-time:' + str(ev["__currthreadptid"])))
                rr_time = float(resp[resp.rindex(b' ') + 1:])

                print(time.strftime("%H:%M:%S"), " - reached gc run ", self._gc_seq_num, " - time ", rr_time, " event: ", rr_event)
                #for s in self._db["subjects"]:
                #    print("            - ", s, " has ", len(list(self._db["subjects"][s].items())[0][1]), " entries")
                #if self._gc_seq_num == 60:
                #    gdb.Breakpoint("MVM_frame_dispatch")

        print(time.strftime("%H:%M:%S"), " - starting")

        self._db_file = tempfile.NamedTemporaryFile(prefix="moar-gdb-rrdb-", suffix=".sqlite3", delete=False)
        self._db_file.close()
        self._db_conn = sqlite3.connect(self._db_file.name, autocommit=False)
        self._db_cur = self._db_conn.cursor()

        print("sqlite3 file can be found at ", self._db_file.name)

        self._db_cur.execute("""
            create table compunits (
                tc integer,
                rr_tick integer,
                rr_event integer,
                data_addr integer
            );
        """)

        self._db_cur.execute("""
            create table staticframes (
                rr_tick integer,
                rr_event integer,
                bytecode integer,
                compunit_data_addr integer,
                sf_addr integer,
                cuuid varchar,
                name varchar
            );
        """)

        self._db_cur.execute("""
            create table spesh_bytecode (
                rr_tick integer,
                rr_event integer,
                sf_addr integer,
                bytecode_addr integer,
                size integer
            );
        """)

        self._db_cur.execute("""
            create table gcs (
                tc integer,
                rr_tick integer,
                rr_event integer,
                gc_seq_number integer,
                is_full integer
            );
        """)

        self._db_cur.execute("""
            create table worklist_runs (
                tc integer,
                rr_tick integer,
                rr_event integer,
                nursery_alloc integer,
                nursery_alloc_limit integer,
                nursery_tospace integer,
                nursery_fromspace integer
            );
        """)

        self._db_cur.execute("""
            create table continuation_events (
                tc integer,
                rr_tick integer,
                rr_event integer,
                tag integer,
                is_slice boolean
            );
        """)


        self._db_cur.execute("""
            create table event_times (
                rr_event integer,
                rr_time float
            );
        """)

        self._db_cur.execute("""
            create table object_movements (
                rr_event integer,
                to_addr integer,
                from_addr integer
            );
        """)

        self._db_cur.execute("""
            create table sc_code (
                sf_addr integer,
                sc_addr integer,
                idx integer
            );
        """)

        self._db_cur.execute("""
            create table meta_info (
                key varchar,
                value varchar
            );
        """)

        self._db_cur.execute("""
            insert into meta_info VALUES (:key, :value)
        """, {"key":"info inferiors", "value": gdb.execute("info inferiors", False, True)})

        self._db_conn.commit()

        EXP = self._EXP()

        self._disabled_breakpoints = []
        self._created_breakpoints = []
        cbp = self._created_breakpoints

        for bp in gdb.breakpoints():
            if bp.enabled:
                bp.enabled = False
                self._disabled_breakpoints.append(bp)

        cbp.append(SQLRecordingBreakpoint(self, "gcs", "tc rr_tick rr_event gc_seq_number is_full", [
            EXP.local_var("tc", "tc", int),
            EXP.local_fields("tc", ["instance"],
                             [("gc_seq_number", "gc_seq_number"),
                              ("is_full", "gc_full_collect")], int),
            ], "run_gc"))

        cbp.append(SQLRecordingBreakpoint(self, "compunits", "tc rr_tick rr_event data_addr", [
            EXP.local_var("tc", "tc", int),
            EXP.local_field("data_addr", "cu", ["body", "data_start"], int),
            ], "run_comp_unit"))

        cbp.append(SQLRecordingBreakpoint(self, "worklist_runs", "tc rr_tick rr_event nursery_alloc nursery_alloc_limit nursery_tospace nursery_fromspace", [
            EXP.local_var("tc", "tc", int),
            EXP.local_fields("tc", [],
                             [("nursery_alloc", "nursery_alloc"),
                              ("nursery_alloc_limit", "nursery_alloc_limit"),
                              ("nursery_fromspace", "nursery_fromspace"),
                              ("nursery_tospace", "nursery_tospace"),
              ], int),
            ], "process_worklist")._extra(store_seq_num))

        if "notrackgc" not in self.flags:
            gdb.execute("list process_worklist", False, True)
            forwarder_update_lineno = gdb.execute("search item->sc_forward_u.forwarder = new_addr;", False, True).split("\t")[0]

            cbp.append(ObjectMovementRecordingBreakpoint(self, "src/gc/collect.c:" + forwarder_update_lineno))
        else:
            print("notrackgc passed. Will not record every object's old and new address.")

        #gdb.execute("list MVM_frame_dispatch", False, True)
        #dispatch_trampoline_lineno = gdb.execute("search MVM_jit_code_trampoline(tc);", False, True).split("\t")[0]

        #cbp.append(ArrayEntryRecordingBreakpoint(self, "calls", [
        #    #EXP.gdb_eval("tc", "tc", int),
        #    EXP.gdb_eval("bytecode_addr", "chosen_bytecode", int),
        #    EXP.gdb_eval("frame", "frame", int),
        #    ], "src/core/frame.c:" + dispatch_trampoline_lineno))

        #print("trying to find a line in MVM_frame_move_to_heap")
        #gdb.execute("list MVM_frame_move_to_heap", False, True)
        #get_to_promote_sf_lineno = gdb.execute("search MVMStaticFrame *sf = cur_to_promote->static_info;", False, True).split("\t")[0]
        #print("oh well")

        gdb.execute("list MVM_spesh_candidate_add", False, True)
        free_speshcode_lineno = gdb.execute("search MVM_free.sc.;", False, True).split("\t")[0]

        cbp.append(SQLRecordingBreakpoint(self, "spesh_bytecode", "rr_tick rr_event sf_addr bytecode_addr size", [
            EXP.local_field("sf_addr", "p", ["sf"], int),
            EXP.local_fields("candidate", ["body"], [
                                 ["bytecode_addr", "bytecode"],
                                 ["size", "bytecode_size"]
                             ], int),
            ], "src/6model/reprs/MVMSpeshCandidate.c:" + free_speshcode_lineno))

        cbp.append(SQLRecordingBreakpoint(self, "staticframes", "rr_tick rr_event bytecode compunit_data_addr sf_addr cuuid name", [
            EXP.local_field("bytecode", "static_frame", ["body", "bytecode"], int),
            EXP.local_field("compunit_data_addr", "static_frame", ["body", "cu", "body", "data_start"], int),
            EXP.local_var("sf_addr", "static_frame", int),
            EXP.local_fields("static_frame", ["body"], [
                                 ["cuuid", "cuuid"],
                                 ["name", "name"],
                             ], mvmstr_to_str),
            ], "MVM_validate_static_frame"))

        cbp.append(SQLRecordingBreakpoint(self, "continuation_events", "tc rr_tick rr_event tag is_slice", [
            EXP.local_var("tc", "tc", int),
            EXP.local_var("tag", "tag", int),
            lambda _: ["is_slice", 0],
            ], "MVM_callstack_continuation_slice"))

        cbp.append(SQLRecordingBreakpoint(self, "continuation_events", "tc rr_tick rr_event tag is_slice", [
            EXP.local_var("tc", "tc", int),
            EXP.local_var("tag", "update_tag", int),
            lambda _: ["is_slice", 1],
            ], "MVM_callstack_continuation_append"))

        #cbp.append(SQLRecordingBreakpoint(self, "sc_code", [
        #    EXP.gdb_eval("sc_addr", "sc->body", int),
        #    EXP.gdb_eval("sf_addr", "code->body.static_frame", int),
        #    EXP.gdb_eval("idx", "idx", int),
        #    ], "MVM_sc_set_code"))

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
        print("Moar RRDB: Finished running. Tearing down breakpoints.")
        for bp in self._disabled_breakpoints:
            bp.enabled = True
        for bp in self._created_breakpoints:
            bp.delete()


    def run(self):
        print("Moar RRDB: Starting the run: Going back to the start ...")
        gdb.execute("start")
        # run to the temporary breakpoint that "start" creates
        print("Moar RRDB: Running to temporary start breakpoint")
        gdb.execute("c")
        # run the program actually
        print("Moar RRDB: Running the program for real ...")
        gdb.execute("c")

    def invoke(self, argument, from_tty):
        global execution_db
        self.flags = set(argument.split(" "))
        self.dont_repeat()
        self.setup()
        try:
            self.run()
        finally:
            try:
                gdb.execute("info breakpoints")
            except:
                pass
            try:
                execution_db = self._db_conn
                gdb.set_convenience_variable("moar_rrdb_file", self._db_file.name)
            except Exception as e:
                print("Could not store the name of the rrdb file in convenience var :(", e)
            self.teardown()

@dataclass
class TimelineEntry:
    pos_event: int

    earliest_event: int | None
    latest_event: int | None

    pos_time: float

    earliest_time: float | None
    latest_time: float | None

class MoarTimelineCommand(gdb.Command):
    """Show recent and upcoming events from moar rrdb's execution db"""

    flags : Set[str]

    def __init__(self):
        super(MoarTimelineCommand, self).__init__("moar timeline", gdb.COMMAND_STATUS)

    def invoke(self, argument, from_tty):
        global execution_db

        if execution_db is None:
            filename_maybe = gdb.parse_and_eval("$moar_rrdb_file")
            # print(f"Can I load db from $moar_rrdb_file? ({filename_maybe})")
            if str(filename_maybe) and str(filename_maybe) != "void" and Path(filename_maybe.string()).exists():
                import sqlite3
                print("Loading previous execution db from ", filename_maybe)
                execution_db = sqlite3.connect(filename_maybe.string(), autocommit=False)
            else:
                print("No execution DB seems to exist. run `moar rrdb` first!")
                self.dont_repeat()
                return

        rr_event = int(gdb.execute("when", False, True).replace("Completed event: ", "").replace("\n", ""))
        rr_time  = float(gdb.execute("elapsed-time", False, True).replace("Elapsed Time (s): ", "").replace("\n", ""))

        try:
            cur = execution_db.cursor()

            query = """
                select
                   igcs.earliest,
                   eet.rr_time as earliest_time,
                   let.rr_time as latest_time,
                   igcs.*
                from
                    (select
                        abs(min(rr_event) - ?) as absdiff,
                        min(rr_event) as earliest,
                        max(rr_event) as latest,
                        gc_seq_number,
                        is_full
                    from gcs
                    where is_full = ?
                    group by gc_seq_number
                    order by 1 asc) igcs
                    left join event_times eet on igcs.earliest = eet.rr_event
                    left join event_times let on igcs.latest = let.rr_event
                limit ?;
            """


            timeline_events = []
            for (want_full, limit) in [(0, 8), (1, 4)]:
                nearby_gcs = cur.execute(query, (rr_event, want_full, limit)).fetchall()
                timeline_events.extend(nearby_gcs)

            timeline_events = sorted(timeline_events, key=lambda e: e[0])
            before = [e for e in timeline_events if e[0] <  rr_event]
            after  = [e for e in timeline_events if e[0] >= rr_event]

            def gc_event_line(earliest, earliest_time, latest_time, absdiff, _, latest, gc_seq_number, is_full):
                print(f"{"maj" if is_full else "min"} GC run {gc_seq_number:5d}: {earliest_time:8.3f}s - {latest_time:8.3f}s ({earliest} - {latest})")

            for e in before:
                gc_event_line(*e)

            print("you are here: ", rr_time, "        ", rr_event, " <-- you are here")

            for e in after:
                gc_event_line(*e)

        except Exception as ex:
            print("Failed to get timeline stuff: ", ex)

        finally:
            cur.close()

#     _             _   _                                                     _
#    | |__  __ _ __| |_| |_ _ _ __ _ __ ___   __ ___ _ __  _ __  __ _ _ _  __| |___
#    | '_ \/ _` / _| / /  _| '_/ _` / _/ -_) / _/ _ \ '  \| '  \/ _` | ' \/ _` (_-<
#    |_.__/\__,_\__|_\_\\__|_| \__,_\__\___| \__\___/_|_|_|_|_|_\__,_|_||_\__,_/__/
#

def can_read(val: gdb.Value):
    try:
        val.cast(uint32p_t).dereference()
    except gdb.MemoryError:
        return False
    return True

def can_read_path(val: gdb.Value, *path: str):
    for step in path:
        try:
            val = val[step]
            try:
                val.cast(uint32p_t).dereference()
            except gdb.error as ex:
                # print("harmless gdb error: ", ex)
                pass
        except gdb.MemoryError:
            return False
    return True

class EarlyAbortException(Exception):
    pass

def is_tc_plausible(tc: gdb.Value, depth : int = 0, score : int = 0):
    # Plausibility check, since arguments in backtraces are sometimes
    # mangled when they are not explicitly stored in a way the debug
    # symbols tell us ...

    if str(tc.type) != "MVMThreadContext *":
        return -math.inf

    def deduct(n):
        nonlocal score
        score -= n
        if score < -500:
            raise EarlyAbortException("score bottomed out")

    try:
        if not can_read_path(tc, "thread_obj", "body"):
            deduct(100)
        else:
            if int(tc["thread_obj"]["body"]["tc"]) != int(tc):
                deduct(250)
            if not (0 <= int(tc["thread_obj"]["body"]["stage"]) <= 6):
                deduct(100)

        if not can_read_path(tc, "instance", "main_thread"):
            deduct(100)

        if not can_read(tc["nursery_tospace"]):
            deduct(200)
        if not can_read(tc["nursery_alloc"]):
            deduct(200)
        if not can_read(tc["nursery_fromspace"]):
            deduct(25)
        if not can_read(tc["gen2"]):
            deduct(100)
        if int(tc["cur_frame"]) != 0 and not can_read_path(tc, "cur_frame", "static_info"):
            deduct(150)
        if int(tc["num_finalize"]) >= int(tc["alloc_finalize"]):
            deduct(50)
        if int(tc["num_temproots"]) >= int(tc["alloc_temproots"]):
            deduct(50)
        if not (1024 <= int(tc["nursery_fromspace_size"]) <= 4 * 4194304):
            deduct(50)

    except EarlyAbortException:
        return -math.inf

    return score

def find_tc():
    frame = gdb.selected_frame()
    found_tcs = []

    # First, let's try the thread-local storage value
    try:
        tls_tc = gdb.parse_and_eval("MVM_running_threads_context")
        # If there's no reason to doubt this tc is right, take it immediately
        if is_tc_plausible(tls_tc) == 0:
            return tls_tc
        found_tcs.append(tls_tc)
    except gdb.GdbError as ex:
        print("Could not get TC from MVM_running_threads_context: ", ex)

    addrs_seen = set()

    while frame is not None:
        tc_value = frame.read_var("tc")
        if tc_value.is_optimized_out:
            pass
        else:
            if int(tc_value) not in addrs_seen:
                found_tcs.append(tc_value)
                addrs_seen.add(int(tc_value))

        frame = frame.older()

    tcs_by_score = sorted(found_tcs, key = lambda ts: is_tc_plausible(ts))
    # for tc in found_tcs:
       # print(" found a TC with value ", hex(tc), " with score ", is_tc_plausible(tc))

    return found_tcs[0]

def frame_effective_bytecode(frame):
    spesh_cand = frame["spesh_cand"]
    if int(spesh_cand) != 0:
        if int(spesh_cand["body"]["jitcode"]) != 0:
            return spesh_cand["body"]["jitcode"]["bytecode"]
        return spesh_cand["body"]["bytecode"]
    return frame["static_info"]["body"]["bytecode"]

def string_from_cu(cu, index):
    strp = cu["body"]["strings"][index]

    if int(strp) == 0:
        # not decoded yet, have to do it in here
        strheap = cu["body"]["string_heap_start"]
        found_idx = 0
        while found_idx < index:
            entrysize = int(int(strheap.cast(uint32p_t).dereference()) // 2)
            if entrysize & 3:
                entrysize += 4 - (entrysize & 3)
            strheap = strheap + (int(entrysize) + 4)
            found_idx += 1

        size_and_flag = int(strheap.cast(uint32p_t).dereference())
        entrysize = size_and_flag // 2

        data = (strheap + 4).string("utf-8", "backslashreplace", entrysize)
        return data
    else:
        return mvmstr_to_str(strp.dereference())

def resolve_annotation(sfb, offset):
    if not (sfb["num_annotations"] >= 0 and offset >= 0 and offset < sfb["bytecode_size"]):
        return (None, None)

    ann_offs = 0
    cur_anno = sfb["annotations_data"]
    p_cur_anno = None
    for i in range(0, sfb["num_annotations"]):
        ann_offs = int(cur_anno.cast(uint32p_t).dereference())

        if ann_offs > offset:
            break

        p_cur_anno = cur_anno
        cur_anno += 12

    if p_cur_anno is not None:
        cur_anno = p_cur_anno

    fnshi = int((cur_anno + 4).cast(uint32p_t).dereference())
    ln    = int((cur_anno + 8).cast(uint32p_t).dereference())

    fn = string_from_cu(sfb["cu"], fnshi)

    return (fn, ln)

def parse_callsite(cs):
    num_flags = cs["flag_count"]
    flags = cs["arg_flags"]

    args = []
    name_idx = 0

    for i in range(num_flags):
        flagvar = int(flags[i])
        arg_type_masked = flagvar & 143
        arg_named_flat_masked = flagvar & 159

        namestr = None

        if flagvar & 32: # MVM_CALLSITE_ARG_NAMED
            namestr = mvmstr_to_str(cs["arg_names"][name_idx])
            name_idx += 1

        typname = ""

        if arg_type_masked == 1: # MVM_CALLSITE_ARG_OBJ
            typname = "obj"
            accessor = lambda u: u["o"]
        elif arg_type_masked == 2: # MVM_CALLSITE_ARG_INT
            typname = "int"
            accessor = lambda u: u["i64"]
        elif arg_type_masked == 4: # MVM_CALLSITE_ARG_NUM
            typname = "num"
            accessor = lambda u: u["n64"]
        elif arg_type_masked == 8: # MVM_CALLSITE_ARG_STR
            typname = "str"
            accessor = lambda u: u["s"].dereference()
        elif arg_type_masked == 128: # MVM_CALLSITE_ARG_UINT
            typname = "uint"
            accessor = lambda u: u["u64"]

        args.append(((namestr + "=" if namestr else "") + (typname or "???"), accessor, [namestr, typname, flagvar, arg_type_masked]))

    return args

class MoarStackFrame:
    """Representation of a frame on moarvm's stack, based on a pointer to
    the MVMFrame struct"""

    class Relation(Enum):
        CALLER = "caller"
        CALLEE = "callee"
        OUTER = "outer"
        INNER = "inner"
        CUR_FRAME = "cur_frame"
        MANUAL = "manual"

    ptr : gdb.Value
    related : MoarStackFrame | gdb.Value | None
    relation : Relation | None

    def __init__(self, ptr : gdb.Value, related : MoarStackFrame | gdb.Value | None = None, relation : Relation| None = None):
        if relation is MoarStackFrame.Relation.CUR_FRAME or relation is MoarStackFrame.Relation.MANUAL:
            assert isinstance(related, gdb.Value)
        elif related is not None:
            assert isinstance(related, MoarStackFrame)

        self.ptr = ptr
        self.related = related
        self.relation = relation

    @classmethod
    def from_tc(cls, tc : gdb.Value | None = None):
        if tc is None:
            tc = find_tc()

        if int(tc["cur_frame"]) == 0:
            return None

        return MoarStackFrame(tc["cur_frame"], tc, MoarStackFrame.Relation.CUR_FRAME)

    @property
    def caller(self):
        caller = self.ptr["caller"]
        if int(caller) != 0:
            return MoarStackFrame(caller, self, MoarStackFrame.Relation.CALLEE)
        else:
            return None

    @property
    def outer(self):
        outer = self.ptr["outer"]
        if int(outer) != 0:
            return MoarStackFrame(outer, self, MoarStackFrame.Relation.INNER)
        else:
            return None

    @property
    def name(self):
        sfb = self.ptr["static_info"]["body"]
        return mvmstr_to_str(sfb["name"].dereference())

    @property
    def cur_op(self):
        if self.relation == MoarStackFrame.Relation.CUR_FRAME:
            assert isinstance(self.related, gdb.Value)
            return self.related["interp_cur_op"].dereference()
        else:
            return self.ptr["return_address"]

    @property
    def bytecode_offs(self):
        sfb = self.ptr["static_info"]["body"]
        bc = frame_effective_bytecode(self.ptr)
        return int(self.cur_op) - int(bc)

    @property
    def params(self):
        return self.ptr["params"]

    @property
    def param_vals(self):
        callsite = self.params["arg_info"]["callsite"]
        csinfo = parse_callsite(callsite)

        return [
            self.params["arg_info"]["source"][
                self.params["arg_info"]["map"][i]
            ] for i in range(len(csinfo))]

    @property
    def cuuid(self):
        return mvmstr_to_str(self.ptr["static_info"]["body"]["cuuid"])

    @property
    def cufile(self):
        return mvmstr_to_str(self.ptr["static_info"]["body"]["cu"]["body"]["filename"])

    def resolve_annotation(self, offs : int | None = None):
        if offs is None:
            offs = self.bytecode_offs

        sfb = self.ptr["static_info"]["body"]

        return resolve_annotation(sfb, offs)

def extract_moar_stack_frame_args(cur_frame):
    # TODO extract to global scope and lookup on init
    stooge_t = gdb.lookup_symbol("MVMObjectStooge")[0].type.strip_typedefs()
    stoogep_t = stooge_t.pointer()
    mvmstr_t = gdb.lookup_symbol("MVMString")[0].type
    mvmstrp_t = mvmstr_t.pointer()

    callsite = cur_frame.params["arg_info"]["callsite"]
    csinfo = parse_callsite(callsite)
    param_vals = cur_frame.param_vals

    infoparts = []

    for i, csi in enumerate(csinfo):
        # info = csinfo[i][0]
        subpart  = csi[1](param_vals[i])
        typename = csi[2][1]
        argname  = csi[2][0]

        string_of_subpart = None

        #print("adding arg of", typename, argname, repr(subpart))
        if typename == "obj":
            flags1 = int(subpart["header"]["flags1"])
            if flags1 & 2: # MVM_CF_STABLE
                is_obj = False
                pass
            elif flags1 & 1: # MVM_CF_TYPE_OBJECT
                is_obj = True
                is_concrete = False
            elif flags1 & 4: # MVM_CF_FRAME
                is_obj = False
            else:
                is_obj = True
                is_concrete = True

            if is_obj:
                reprname = subpart["st"]["REPR"]["name"].string()
                if reprname == "P6str" and is_concrete:
                    #print("casting to p6str? before:")
                    #print(repr(subpart), repr(subpart.type))
                    subpart = subpart.cast(stoogep_t)["data"].cast(mvmstrp_t)
                    #print(repr(subpart), repr(subpart.type))
                    #print("trying to mvmstr_to_str this:", repr(mvmstr_to_str(subpart)))
                    string_of_subpart = "((MVMString *)" + hex(int(subpart)) + ")=" + repr(mvmstr_to_str(subpart, truncate=128))
                elif int(subpart["st"]["debug_name"]) != 0:
                    typename = reprname + "#" + subpart["st"]["debug_name"].string()
                    if is_concrete:
                        typename = typename + ".new"

        if string_of_subpart is None:
            string_of_subpart = gdb.printing.make_visualizer(subpart).to_string()
        if string_of_subpart is None:
            string_of_subpart = "<???>"

        infoparts.append(
            (argname + "=" if argname else "")
            + typename
            + "("
            + string_of_subpart
            + ")")

    return infoparts

class MoarBtCommands(gdb.Command):
    """Commands to look at a moar-level backtrace."""
    def __init__(self):
        super(MoarBtCommands, self).__init__("moar bt", gdb.COMMAND_STACK, prefix=True)

    def invoke(self, argument, from_tty):
        stooge_t = gdb.lookup_symbol("MVMObjectStooge")[0].type.strip_typedefs()
        stoogep_t = stooge_t.pointer()
        mvmstr_t = gdb.lookup_symbol("MVMString")[0].type
        mvmstrp_t = mvmstr_t.pointer()

        tc = find_tc()
        cur_frame : MoarStackFrame = MoarStackFrame.from_tc(tc)

        stack_idx = 0

        while cur_frame is not None:
            name = cur_frame.name
            fn, ln = cur_frame.resolve_annotation()

            infoparts = extract_moar_stack_frame_args(cur_frame)

            csinfo_str = "args=(" + ", ".join(infoparts) + ")"

            # Instead of just outputting "'' at <unknown>:1", make an attempt
            # to get at least something to differentiate one thing from another
            outer_str = ""
            if not name and (fn is None or ln is None):
                outer_frame = cur_frame.outer
                print("outer frame is: ", outer_frame)
                outer_fn, outer_ln = outer_frame.resolve_annotation(0)
                outer_locstr = ""
                if outer_fn is not None and outer_ln is not None:
                    outer_locstr = f" {fn}:{ln}"
                if outer_frame is not None:
                    if outer_frame.name is not None and outer_frame.name:
                        outer_str = f"(nested inside '{outer_frame.name}'{outer_locstr})"
                    else:
                        outer_str = f"(nested inside {outer_frame.cuuid}{outer_locstr})"

            name = f"'' ({cur_frame.cuuid})" if name == "" else name
            if fn is not None and ln is not None:
                locstr = f"{fn}:{ln}"
            else:
                locstr = "<unknown>:1"

                fn, ln = cur_frame.resolve_annotation(offs=0)
                if fn is not None and ln is not None:
                    locstr = f"somewhere after {fn}:{ln}"

            if outer_str:
                locstr = locstr + " " + outer_str

            print(f"#{stack_idx}", cur_frame.ptr, name, csinfo_str, locstr)

            cur_frame = cur_frame.caller
            stack_idx += 1


def do_single_frame_command_stuff(cur_frame : MoarStackFrame, stack_idx = None):
    fn, ln = cur_frame.resolve_annotation()
    infoparts = extract_moar_stack_frame_args(cur_frame)

    name = cur_frame.name
    name = "''" if name == "" else name

    loc = ""

    if fn is None or ln is None:
        loc = f"{fn} : {ln}"
    else:
        loc = f"<unknown>:1"

    if stack_idx is not None:
        print(f"Stack Frame #{stack_idx}")

    print(cur_frame.ptr)
    print(f"Name: {name} {loc}")
    print(f"Bytecode file: {cur_frame.cufile} ; cuuid {cur_frame.cuuid}")
    print("")
    if cur_frame.caller is not None:
        print(f"Caller: {cur_frame.caller.ptr}")
    if cur_frame.outer is not None:
        print(f"Outer: {cur_frame.outer.ptr}")

    print("Arguments:")
    for info in infoparts:
        print(f"  {info}")
    print("")

    csinfo = parse_callsite(cur_frame.params["arg_info"]["callsite"])
    param_vals = cur_frame.param_vals

    prev_margs_cnt = 0
    unset_val = None
    try:
        prev_margs_cnt_val = gdb.parse_and_eval("$margs_cnt")
        if prev_margs_cnt_val.type.name != "void":
            prev_margs_cnt = int(prev_margs_cnt_val)
        unset_val = gdb.parse_and_eval("$okay_so_hear_me_out_i_need_a_gdb_value_that_is_void_but_not_a_void_pointer_or_anything_and_i_dont_know_how_to_create_it_from_python_so_please_dont_put_anything_in_this_variable_thank_you")
    except:
        print("could not unset obsolete margs convenience vars")

    gdb.set_convenience_variable("mframe", cur_frame.ptr)
    print(f"var $mframe set to {cur_frame.ptr}")

    gdb.set_convenience_variable("margs_cnt", len(csinfo))
    print(f"var $margs_cnt set to {len(csinfo)}")
    for i, csi in enumerate(csinfo):
        # info = csinfo[i][0]
        subpart  = csi[1](param_vals[i])

        typename = csi[2][1]

        if typename == "obj":
            is_obj = False
            is_concrete = False
            flags1 = int(subpart["header"]["flags1"])
            if flags1 & 2: # MVM_CF_STABLE
                is_obj = False
            elif flags1 & 1: # MVM_CF_TYPE_OBJECT
                is_obj = True
                is_concrete = False
            elif flags1 & 4: # MVM_CF_FRAME
                is_obj = False
            else:
                is_obj = True
                is_concrete = True

            if is_obj and is_concrete:
                reprname = subpart["st"]["REPR"]["name"].string()
                if reprname in repr_infos:
                    typ = repr_infos[reprname]["struct"].pointer()
                    subpart = subpart.cast(typ)

        varname = f"margs_{i}"
        gdb.set_convenience_variable(varname, subpart)
        print(f"var ${varname} set to ({subpart.type}) {subpart}")

    try:
        for i in range(len(csinfo), prev_margs_cnt):
            varname = f"margs_{i}"
            gdb.set_convenience_variable(varname, unset_val)
            print(f"var ${varname} unset")
    except:
        print("could not unset obsolete margs convenience vars")


class MoarBtFrameCommand(gdb.Command):
    """Get all details of one stack frame on the moarvm stack"""
    def __init__(self):
        super(MoarBtFrameCommand, self).__init__("moar bt frame", gdb.COMMAND_STACK, prefix=True)

    def invoke(self, argument, from_tty):
        stack_idx = None
        if argument == "" or argument is None:
            cur_frame = MoarStackFrame.from_tc()
        elif argument[0] == "*":
            evaled = gdb.parse_and_eval(argument[1:])
            cur_frame = MoarStackFrame(evaled, evaled, MoarStackFrame.Relation.MANUAL)
        else:
            evaled = int(gdb.parse_and_eval(argument))
            cur_frame = MoarStackFrame.from_tc()
            stack_idx = 0
            while stack_idx < evaled and cur_frame is not None:
                cur_frame = cur_frame.caller
                stack_idx += 1
            if cur_frame is None:
                print(f"Frame with index {evaled} could not be found; stack exhausted after {stack_idx} steps!")

        do_single_frame_command_stuff(cur_frame, stack_idx)


class MoarBtFrameUpCommand(gdb.Command):
    """Get all details of the current moar stack frame's caller."""
    def __init__(self):
        super(MoarBtFrameUpCommand, self).__init__("moar bt frame up", gdb.COMMAND_STACK)

    def invoke(self, argument, from_tty):
        frame = gdb.parse_and_eval("$mframe")
        if int(frame) != 0:
            last_frame = MoarStackFrame(frame, frame, MoarStackFrame.Relation.MANUAL)
            if last_frame.caller is None:
                print("Last frame on the stack reached!")
                self.dont_repeat()
                return

            do_single_frame_command_stuff(last_frame.caller)
        else:
            raise ValueError("Convenience Variable $mframe doesn't seem to exist?")

class MoarBtFrameOutCommand(gdb.Command):
    """Get all details of the current moar stack frame's outer."""
    def __init__(self):
        super(MoarBtFrameOutCommand, self).__init__("moar bt frame out", gdb.COMMAND_STACK)

    def invoke(self, argument, from_tty):
        frame = gdb.parse_and_eval("$mframe")
        if int(frame) != 0:
            last_frame = MoarStackFrame(frame, frame, MoarStackFrame.Relation.MANUAL)
            if last_frame.outer is None:
                print("This frame has no more outers!")
                self.dont_repeat()
                return

            do_single_frame_command_stuff(last_frame.outer)
        else:
            raise ValueError("Convenience Variable $mframe doesn't seem to exist?")

class MoarTCCommand(gdb.Command):
    """More-or-less reliably get the currently active thread's MVMThreadContext"""
    def __init__(self):
        super(MoarTCCommand, self).__init__("moar tc", gdb.COMMAND_STACK)

    def invoke(self, argument, from_tty):
        tc = find_tc()
        histval = gdb.add_history(tc)

        try:
            gdb.execute(f"print ${histval}")
        except:
            pass

        gdb.set_convenience_variable("TC", tc)
        print(f"var $TC set to ({tc.type}) {tc}")

#                  _   _                   _     _
#     _ __ _ _ ___| |_| |_ _  _   _ __ _ _(_)_ _| |_ ___ _ _ ___
#    | '_ \ '_/ -_)  _|  _| || | | '_ \ '_| | ' \  _/ -_) '_(_-<
#    | .__/_| \___|\__|\__|\_, | | .__/_| |_|_||_\__\___|_| /__/
#    |_|                   |__/  |_|

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
    commands.append(MoarCommands())

    # currently the analyze and diff heap commands don't work
    #commands.append(AnalyzeHeapCommand())
    #print("command moar heap registered")
    #commands.append(DiffHeapCommand())
    #print("command moar diff-heap registered")

    commands.append(MakeExecutionDatabaseCommand())
    print("command moar rrdb registered")

    commands.append(MoarTimelineCommand())
    print("command moar timelineregistered")

    commands.append(MoarBreakCommands())
    commands.append(MoarBreakInterpRunCommands())
    commands.append(MoarBreakExceptionAdhoc())
    print("moar break commands registered")

    commands.append(MoarBtCommands())
    commands.append(MoarBtFrameCommand())
    commands.append(MoarBtFrameUpCommand())
    commands.append(MoarBtFrameOutCommand())
    print("moar bt commands registered")

    commands.append(MoarTCCommand())
    print("moar TC commands registered")

# We have to introduce our classes to gdb so that they can be used
if __name__ == "__main__":
    the_objfile = gdb.current_objfile()
    if the_objfile is None:
        try:
            the_objfile = gdb.lookup_objfile("libmoar.so")
        except:
            print("GDB doesn't know about 'libmoar.so' yet; maybe you need to run the program a little until it's loaded:")
            print("    break MVM_interp_run")
            print("    c")
    if the_objfile:
        register_printers(the_objfile)
    register_commands(the_objfile)

    try:
        uint32_t  = gdb.lookup_symbol("MVMuint32")[0].type.strip_typedefs()
        uint32p_t = uint32_t.pointer()
    except gdb.error as ex:
        print("Couldn't get MVMuint32 type!?", ex)

    init_repr_types_and_structs()
