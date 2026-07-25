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
# - An almost complete pretty-printer for MVMString and MVMString*
# - A group of commands "moar break" for commonly useful breakpoints.
# - A command "moar bt" that shows a backtrace including each frame's arguments.
# - Commands `moar frame N` that lets you get a frame by index on the current
#   stack, `moar frame up` that lets you move to the last frame's caller, or
#   `moar frame out` that lets you move to the last frame's outer.
# - A command `moar tc` that works out the address of the current thread's
#   MVMThreadContext without getting confused by the occasionally wrong
#   values you see in gdb's backtrace command.
#
# There are some things in the code that are incomplete or not working:
# - A pretty-printer for MVMObject in general
# - A command "moar heap" that walks the nursery and gen2 and displays
#   statistics about the REPRs found in them, as well as a display of
#   the fragmentation of the gen2 pages.
#
#
# If you're in a replay session with rr (rr-project.org), there are some
# additional features you can take advantage of:
#
# - `moar rrdb` runs the program from start to finish while recording
#   information from certain interesting events, storing them in a sqlite3
#   database file. Flags to add more potentially expensive data collection
#   exist. See `help moar rrdb` for more info.
# - `moar timeline` shows things that have happened or will happen around
#   your current position in the replay, including minor and major GC runs
#   as well as frame invokes and returns during the same rr event (`when`).
#

# Here's the TODO list:
#
# - Implement diffing for the gen2 in some sensible manner
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

from contextlib import contextmanager
from array import array
from sqlite3 import Connection
from dataclasses import dataclass, field, fields
from pathlib import Path
from collections.abc import Mapping, Sequence, Set, Callable, Buffer
from enum import Enum
import gdb
from collections import defaultdict
import math
import random
import struct
import sys
import time
import typing
import tempfile

import gdb.printing

import traceback # debugging

uint32_t  : gdb.Type | None
uint32p_t : gdb.Type | None

uint8_t  : gdb.Type | None
uint8p_t : gdb.Type | None

stooge_t  : gdb.Type | None
stoogep_t : gdb.Type | None
mvmstr_t  : gdb.Type | None
mvmstrp_t : gdb.Type | None

# These are the flags from MVMString's body.flags
str_t_info = {0: 'blob_32',
              1: 'blob_ascii',
              2: 'blob_8',
              3: 'strands',
              4: 'in_situ_8',
              5: 'in_situ_32'}

type_to_register_member = {
     1: "i8",   # MVM_reg_int8
     2: "i16",  # MVM_reg_int16
     3: "i32",  # MVM_reg_int32
     4: "i64",  # MVM_reg_int64
     5: "n32",  # MVM_reg_num32
     6: "n64",  # MVM_reg_num64
     7: "s",    # MVM_reg_str
     8: "o",    # MVM_reg_obj
    17: "u8",   # MVM_reg_uint8
    18: "u16",  # MVM_reg_uint16
    19: "u32",  # MVM_reg_uint32
    20: "u64",  # MVM_reg_uint64
}

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


def array_code_for_bytesize(bytesize):
    return [c for c in "BHILQ" if array(c).itemsize == bytesize][0]

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

def mvmstr_to_str(val : gdb.Value, strlen=None, truncate=5000):
    if val.type.code == gdb.TYPE_CODE_PTR and int(val) == 0:
        return "<nullptr>"
    its_st   = val["common"]["st"]
    its_repr = its_st["REPR"]
    reprid   = its_repr["ID"]
    try:
        if int(reprid) != repr_infos["MVMString"]["reprid"]:
            reprname = "??"
            try:
                reprname = its_repr["name"].string()
            except:
                reprname = "<error finding reprname>"
            debugname = "??"
            try:
                debugname = its_st["debug_name"].string()
            except:
                debugname = "<error finding debugname>"
            return "<not a string: " + reprname + " " + debugname + ">"
    except gdb.MemoryError as ex:
        return f"<not a string: {ex}>"
    try:
        stringtyp = str_t_info[int(val['body']['storage_type'])]
    except KeyError:
        return "<invalid string?>"
    if stringtyp in ("blob_32", "blob_ascii", "blob_8", "in_situ_8", "in_situ_32"):
        data = val['body']['storage'][stringtyp]
        pieces = []
        graphs = int(val['body']['num_graphs'])

        if graphs == 0:
            return ""

        truncated = 0

        elemtype = data.type.target()

        memory = bytes(gdb.selected_inferior().read_memory(data, elemtype.sizeof * graphs))
        arraytypecode = "b" if elemtype.sizeof == 1 else "i"
        gdata = array(arraytypecode, memory)

        if strlen is not None and strlen > graphs:
            graphs = strlen

        if graphs > truncate :
            truncated = graphs - truncate
            graphs = truncate

        for i in range(graphs):
            pdata = int(gdata[i])
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
        start = 0

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

class MVMStrHash:
    val: gdb.Value
    _control_val: gdb.Value
    _control_sizeof: int

    def __init__(self, val: gdb.Value):
        self.val = val
        control_type = self.val.referenced_value().type
        self._control_sizeof = control_type.sizeof
        self._entry_sizeof   = self.val["entry_size"]
        self._handle_type    = gdb.lookup_type("struct MVMStrHashHandle").pointer()

    @property
    def official_size(self):
        return 1 << int(self.val["official_size_log2"])

    def entries(self):
        control = self.val
        if int(control) == 0:
            return

        metadata = control.cast(uint8p_t) + self._control_sizeof
        data = control.cast(uint8p_t) - self._entry_sizeof

        entry_sizeof = self._entry_sizeof

        kompromat = self.official_size + self.val["max_probe_distance"] - 1
        for pos in range(1, kompromat + 1):
            if int(metadata[pos - 1]):
                entry_val = data - ((pos - 1) * entry_sizeof)
                yield (pos - 1, int(metadata[pos - 1]), entry_val.cast(self._handle_type))


class StrHashCommand(gdb.Command):
    """Output the contents of an MVMStrHashTable, optionally showing the values
    in the slots in the right type, if given as a second argument.

    Ex: moar hash tc->instance->sc_weakhash.table
    Ex: moar hash tc->instance->sc_weakhash.table "struct MVMSerializationContextWeakHashEntry"
    """
    def __init__(self):
        super(StrHashCommand, self).__init__("moar hash", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        val  = gdb.parse_and_eval(args[0])
        handle_type = None
        if len(args) > 1:
            handle_type = gdb.lookup_type(args[1])
            if handle_type.code != gdb.TYPE_CODE_PTR:
                handle_type = handle_type.pointer()

        entries : list[gdb.Value] = list(MVMStrHash(val).entries())
        out_entries = []

        longest_key = 0
        longest_addr = 0

        for bucket, metadata, e in entries:
            key = ""
            if handle_type is not None:
                ederef = e.cast(handle_type).dereference()
                result_str = str(ederef)
                key = mvmstr_to_str(ederef["hash_handle"]["key"])
                repl_str = "hash_handle = {key = (MVMString *)'" + key + "'}, "
                result_str = result_str.replace(repl_str, "")
                ederef = result_str
                key = repr(key)
                longest_key = max(longest_key, len(key))

            else:
                ederef = e.dereference()

            longest_addr = max(len(str(e)), longest_addr)

            out_entries.append((bucket, metadata, key, e, ederef))

        if longest_key:
            print(f"bkt  meta {"key":{longest_key}s}  {"addr":{longest_addr}s}  value")
        else:
            print(f"bkt  meta   {"addr":{longest_addr}s}  entry")

        for (bucket, metadata, key, e, ederef) in out_entries:
            print((f"{bucket:3x}  [{metadata:2x}] {key:{longest_key}s}  {e}  {ederef}"))
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

@contextmanager
def breakpoints_disabled():
    disabled_breakpoints = []
    for bp in gdb.breakpoints():
        if bp.enabled:
            bp.enabled = False
            disabled_breakpoints.append(bp)
    try:
        yield
    finally:
        for bp in disabled_breakpoints:
            bp.enabled = True

class RrGdbConn:
    conn: gdb.RemoteTargetConnection

    def __init__(self, conn: gdb.RemoteTargetConnection | None  = None):
        if conn is None:
            conns = gdb.connections()
            if len(conns) > 1:
                raise Exception("Don't know how to handle more than one connection?!")
            if not isinstance(conns[0], gdb.RemoteTargetConnection):
                raise Exception("Can only work with RemoteTargetConnection objects, not " + str(type(conns[0])))
            conn = conns[0]

        self.conn = conn

    def ptid(self):
        currthreadptid = self.conn.send_packet("qC")
        return int(currthreadptid[currthreadptid.rindex(b".") + 1:], 16)

    def event(self, ptid: int | None = None):
        if ptid is None:
            ptid = self.ptid()
        resp = bytes.fromhex(self.conn.send_packet('qRRCmd:when:' + str(ptid)))
        return int(resp[resp.rindex(b' '):])

    def tick(self, ptid: int | None = None):
        if ptid is None:
            ptid = self.ptid()
        resp = bytes.fromhex(self.conn.send_packet('qRRCmd:when-ticks:' + str(ptid)))
        return int(resp[resp.rindex(b' '):])

    def time(self, ptid: int | None = None):
        if ptid is None:
            ptid = self.ptid()
        resp = bytes.fromhex(self.conn.send_packet('qRRCmd:elapsed-time:' + str(ptid)))
        return float(resp[resp.rindex(b' ') + 1:])

    def tid (self, ptid: int | None = None):
        if ptid is None:
            ptid = self.ptid()
        resp = bytes.fromhex(self.conn.send_packet('qRRCmd:when-tid:' + str(ptid)))
        return int(resp[resp.rindex(b' ') + 1:])

    def ptid_event_tick(self):
        ptid = self.ptid()
        event = self.event(ptid)
        tick = self.tick(ptid)
        return (ptid, event, tick)

class AutoResumingBreakpoint(gdb.Breakpoint):
    def stop(self):
        try:
            self._hit_breakpoint_action()
        except Exception as ex:
            print(f"exception while evaluating {self}: ", ex)
            raise
        return False # don't actually stop

class PointInTimeRecordingBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, event, exprs, *args, **kwargs):
        super(PointInTimeRecordingBreakpoint, self).__init__(*args, internal=True, **kwargs)
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
    def __init__(self, medbc, table_name, exprs, *args, **kwargs):
        super(ArrayEntryRecordingBreakpoint, self).__init__(*args, internal=True, **kwargs)
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
    def __init__(self, medbc, table_name : str, column_names : str, exprs : Sequence, *args, **kwargs):
        super(SQLRecordingBreakpoint, self).__init__(*args, internal=True, **kwargs)
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

class SQLRecordingBreakpointWithPrereq(SQLRecordingBreakpoint):
    prereq : Callable
    def stop(self):
        try:
            if self.prereq():
                self._hit_breakpoint_action()
        except Exception as ex:
            print("error evaluating bp prereq: ", ex)
        return False

class EventTimeAndStackBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, *args, **kwargs):
        super(EventTimeAndStackBreakpoint, self).__init__(*args, internal=True, **kwargs)
        self._medbc = medbc # MakeExecutionDatabaseCommand

    def _hit_breakpoint_action(self):
        self._medbc._register_event_time_and_stack_only()

class ObjectMovementRecordingBreakpoint(AutoResumingBreakpoint):
    def __init__(self, medbc, *args, **kwargs):
        super(ObjectMovementRecordingBreakpoint, self).__init__(*args, internal=True, **kwargs)
        self._medbc = medbc

    def _hit_breakpoint_action(self):
        self._medbc._register_object_movement()

#  ____  _             _      ____                        _
# / ___|| |_ __ _  ___| | __ |  _ \ ___  ___ ___  _ __ __| | ___ _ __
# \___ \| __/ _` |/ __| |/ / | |_) / _ \/ __/ _ \| '__/ _` |/ _ \ '__|
#  ___) | || (_| | (__|   <  |  _ <  __/ (_| (_) | | | (_| |  __/ |
# |____/ \__\__,_|\___|_|\_\ |_| \_\___|\___\___/|_|  \__,_|\___|_|
#
class StackRecorder:
    """Holds state about stacks of op addresses and their corresponding static
    frame addresses in order to generate a Firefox Profiler UI compatible
    file."""

    @dataclass
    class Func:
        sfaddr : int
        bcaddr : int

    @dataclass
    class StackPiece:
        # frame  : int
        pieces        : array = field(default_factory=lambda: array('L'))
        piece_cur_ops : array = field(default_factory=lambda: array('Q'))

    pid : int

    # Lookup dictionary from address of staticframe to index into the arrays of
    # function properties
    func_by_sfaddr  : dict[int, int]

    # List of "stack pieces", where each has an array of child piece indices
    # and an array of the cur_op addresses of these child pieces.
    stack_pieces    : list[StackPiece]

    # Direct lookup from `cur_op` to the index of a static frame.
    sfidx_by_cur_op  : dict[int, int]
    # List of static frame index to static frame object pointer.
    sfidx_to_sfaddr  : list

    # Arrays of sample properties:

    # Each sample's stack piece index
    stack_piece_of_sample: array
    # Each sample's TID
    thread_of_sample : array

    # sqlite3 database connection for looking up rr_event -> rr_time
    conn : Connection

    def __init__(self, pid : int, conn : Connection):
        self.pid = pid

        self.func_by_sfaddr = dict()
        self.sfidx_by_cur_op = dict()
        self.sfidx_to_sfaddr = list()

        self.stack_pieces = [StackRecorder.StackPiece()]

        self.stack_piece_of_sample = array('L')
        self.thread_of_sample = array('Q')

        self.conn = conn


    def insert_stack_pieces(self, frames : Sequence[tuple[gdb.Value, gdb.Value]], rr_event : int, tid : int):
        tree_pointer = self.stack_pieces[0]
        stack_piece_index = 0

        # reused = 0

        for frm in frames[::-1]:
            assert frm[0] is not None
            assert frm[1] is not None
            assert frm[0].address is not None
            assert frm[1].address is not None
            frma = (int(frm[0].address), int(frm[1].address))

            # Climb the tree of stack pieces
            try:
                found = tree_pointer.piece_cur_ops.index(frma[0])
                stack_piece_index = tree_pointer.pieces[found]
                tree_pointer = self.stack_pieces[stack_piece_index]
                # reused += 1
            except ValueError:
                if frma[0] not in self.sfidx_by_cur_op:
                    sfidx = len(self.sfidx_to_sfaddr)
                    self.sfidx_by_cur_op[frma[0]] = sfidx
                    self.sfidx_to_sfaddr.append(frm[1])
                else:
                    sfidx = self.sfidx_by_cur_op[frma[0]]
                new_piece = StackRecorder.StackPiece()
                stack_piece_index = len(self.stack_pieces)
                tree_pointer.pieces.append(stack_piece_index)
                self.stack_pieces.append(new_piece)
                tree_pointer.piece_cur_ops.append(frma[0])

                tree_pointer = new_piece

        while rr_event >= len(self.stack_piece_of_sample):
            self.stack_piece_of_sample.append(0)
            self.thread_of_sample.append(0)
        if self.thread_of_sample[rr_event] != 0:
            print(f"wtf, thread of sample already had something for {rr_event}")
        self.stack_piece_of_sample[rr_event] = stack_piece_index
        self.thread_of_sample[rr_event] = tid

        # print(f"reused {reused:3d} / {len(frames):3d} | stack pcs: {len(self.stack_pieces):4d} | cur ops: {len(self.sfidx_by_cur_op):4d} | samples {len(self.stack_piece_of_sample)}")

    def dump_as_profile_file(self):
        print("... preparing profile json data ...")
        # For the FrameTable, look up index by address
        frame_idx_by_cur_op : dict[int, int] = dict()
        # Frame index to address, reverse of the above
        cur_ops_for_frame_table : array = array('Q')

        # First, turn the stack tree upside-down, yippie
        parent_indices = list()
        index_into_frame_array = array('L')

        # FrameTable
        # Lookup from frame to func
        sfidx_to_func_index : dict[int, int] = dict()
        # one entry per frame, points into the func array
        index_into_func_array = array('L')
        category_by_frame_idx = array('b')

        # FuncTable
        # one entry per func, points into the string array
        name_indices_by_func = array('L')
        resource_by_func = array('L')

        # Resources (source files)
        resource_idx_by_cu_addr : dict[int, int] = dict()
        resource_name_by_idx = array('L')
        resource_type_by_idx = array('b')

        string_list = []

        index_into_frame_array.append(0)

        def traverse(index, parent=0):
            piece : StackRecorder.StackPiece = self.stack_pieces[index]

            # new value. starts out as 0
            this_index = len(parent_indices)
            # parent index of null means "root node"
            parent_indices.append(parent)

            for i in range(len(piece.pieces)):
                child_piece_index : int = piece.pieces[i]
                cur_op : int = piece.piece_cur_ops[i]

                category_id = 0

                # Make the new frames array entry as well
                if cur_op not in frame_idx_by_cur_op:
                    frame_entry_idx = len(frame_idx_by_cur_op)
                    frame_idx_by_cur_op[cur_op] = frame_entry_idx
                    cur_ops_for_frame_table.append(cur_op)

                    # This may be the first time we've seen the sfidx for the
                    # provided cur_op. If so, create a new entry in the
                    # tables for FuncTable
                    sfidx = self.sfidx_by_cur_op[cur_op]
                    if sfidx not in sfidx_to_func_index:
                        func_index = len(name_indices_by_func)

                        name_of_frame = "error looking up frame name"
                        try:
                            sfaddr = self.sfidx_to_sfaddr[sfidx]
                            name_of_frame = mvmstr_to_str(sfaddr["name"])
                        except IndexError:
                            print(f"Could not get addr at sfidx {sfidx} for addr {cur_op}")

                        try:
                            stridx = string_list.index(name_of_frame)
                        except ValueError:
                            stridx = len(string_list)
                            string_list.append(name_of_frame)

                        name_indices_by_func.append(stridx)
                        index_into_func_array.append(func_index)
                        sfidx_to_func_index[sfidx] = func_index

                        cu_addr = int(sfaddr["cu"])
                        if cu_addr not in resource_idx_by_cu_addr:
                            cu_filename_ptr = sfaddr["cu"]["body"]["filename"]
                            if int(cu_filename_ptr) != 0:
                                try:
                                    cu_filename = mvmstr_to_str(cu_filename_ptr)
                                except:
                                    cu_filename = "(error getting name)"
                            else:
                                cu_filename = "(unknown)"

                            try:
                                stridx = string_list.index(cu_filename)
                            except ValueError:
                                stridx = len(string_list)
                                string_list.append(cu_filename)

                            resource_idx_by_cu_addr[cu_addr] = len(resource_name_by_idx)
                            resource_name_by_idx.append(stridx)

                            if "runtime/CORE." in cu_filename:
                                category_id = 2
                            elif "share/nqp/lib" in cu_filename:
                                category_id = 3
                            elif "perl6/lib" in cu_filename:
                                category_id = 4
                            elif 'share/perl6' in cu_filename:
                                category_id = 5
                            else:
                                category_id = 1

                            # print(f"added new category id into resource_type_by_idx at index {len(resource_type_by_idx)}")
                            resource_type_by_idx.append(category_id)
                        else:
                            ridx = resource_idx_by_cu_addr[cu_addr]
                            # print(f"looking for category of resource idx {ridx} of cu addr {cu_addr}")
                            category_id = resource_type_by_idx[ridx]

                        resource_by_func.append(resource_idx_by_cu_addr[cu_addr])

                    else:
                        func_index = sfidx_to_func_index[sfidx]
                        index_into_func_array.append(func_index)
                        ridx = resource_by_func[func_index]
                        category_id = resource_type_by_idx[ridx]

                else:
                    frame_entry_idx = frame_idx_by_cur_op[cur_op]

                    sfidx = self.sfidx_by_cur_op[cur_op]
                    sfaddr = self.sfidx_to_sfaddr[sfidx]
                    cu_addr = int(sfaddr["cu"])
                    ridx = resource_idx_by_cu_addr[cu_addr]
                    category_id = resource_type_by_idx[ridx]

                category_by_frame_idx.append(category_id)
                index_into_frame_array.append(frame_entry_idx)

                # the new index child_piece_index is an index into the "old"
                # stack pieces array. A new index will be created for it inside
                # the call and the offset is calculated from that new index
                # and "this_index" being the parent.
                traverse(child_piece_index, this_index)

        traverse(0, None)

        print("... preparing samples ...")

        all_tids = sorted(set(self.thread_of_sample))
        print(f"   ... got samples for {len(all_tids)} TIDs")

        stacks_per_thread = {tid: array('L') for tid in all_tids}
        times_per_thread  = {tid: array('d') for tid in all_tids}

        cur = self.conn.cursor();
        cur.execute("select rr_event, rr_time from event_times;")
        time_of_event = {e[0]: e[1] for e in cur.fetchall()}

        dropped_samples = 0
        for idx in range(len(self.thread_of_sample)):
            tid = self.thread_of_sample[idx]
            if tid == 0:
                continue

            rr_event = idx

            try:
                times_per_thread[tid].append(time_of_event[rr_event] * 1000)
                stacks_per_thread[tid].append(self.stack_piece_of_sample[idx])
            except KeyError:
                dropped_samples += 1

        if dropped_samples:
            print(f"   ... dropped {dropped_samples} out of {len(self.stack_piece_of_sample)} samples; have times for {len(time_of_event)} events")


        def samples_for_thread(tid):
            return dict(
                stack=list(stacks_per_thread[tid]),
                time=list(times_per_thread[tid]),
                weight=None,
                weightType="samples",
                length=len(stacks_per_thread[tid]),
            )


        threads = [dict(
            processType="default",
            processStartupTime=0,
            processShutdownTime=None,
            registerTime=0,
            unregisterTime=None,
            pausedRanges=[],
            name="Raku frames!",
            isMainThread=False,
            pid=self.pid,
            tid=str(tid) + ".raku",
            samples=samples_for_thread(tid),
            markers=dict(
                data=[],
                name=[],
                startTime=[],
                endTime=[],
                phase=[],
                category=[],
                length=0,
            ),
        ) for tid in all_tids]

        if len(index_into_frame_array) != len(parent_indices):
            print(f"    !!!!  length mismatch for stackTable: {len(index_into_frame_array)} vs {len(parent_indices)}")

        if len(cur_ops_for_frame_table) != len(index_into_func_array):
            print(f"    !!!!  length mismatch for frameTable: {len(cur_ops_for_frame_table)} vs {len(index_into_func_array)}")

        def cat(name, color):
            return dict(name=name, color=color, subcategories=["Other"])

        categories = [
            cat("Other",    "grey"),
            cat("User",   "yellow"),
            cat("Core", "lightred"),
            cat("NQP", "lightblue"),
            cat("Rakudo", "maroon"),
            cat("Modules", "green"),
        ]

        import json
        print("... serializing data")
        result = json.dumps(dict(
            meta=dict(
                interval=1000,
                startTime=0,
                processType=0,
                product="moarvm rrdb",
                stackwalk=1,
                version=24,
                preprocessedProfileVersion=60,
                sourceCodeIsNotOnSearchfox=True,
                markerSchema=[],
                categories=categories,
            ),
            libs=[],
            shared=dict(
                stackTable=dict(
                    frame=list(index_into_frame_array),
                    prefix=list(parent_indices),
                    length=len(index_into_frame_array)
                ),
                frameTable=dict(
                    address=list(cur_ops_for_frame_table),
                    length=len(cur_ops_for_frame_table),
                    inlineDepth=[0] * len(cur_ops_for_frame_table),
                    category=list(category_by_frame_idx),
                    subcategory=[None] * len(cur_ops_for_frame_table),
                    innerWindowID=[None] * len(cur_ops_for_frame_table),
                    nativeSymbol=[None] * len(cur_ops_for_frame_table),
                    line=[None] * len(cur_ops_for_frame_table),
                    column=[None] * len(cur_ops_for_frame_table),
                    func=list(index_into_func_array),
                ),
                funcTable=dict(
                    name=list(name_indices_by_func),
                    isJS=[False] * len(name_indices_by_func),
                    relevantForJS=[False] * len(name_indices_by_func),
                    resource=list(resource_by_func),
                    source=[None] * len(name_indices_by_func),
                    lineNumber=[None] * len(name_indices_by_func),
                    columnNumber=[None] * len(name_indices_by_func),
                    originalLocation=[None] * len(name_indices_by_func),
                    length=len(name_indices_by_func),
                ),
                resourceTable=dict(
                      length=len(resource_name_by_idx),
                      lib=[None] * len(resource_name_by_idx),
                      name=list(resource_name_by_idx),
                      host=[0] * len(resource_name_by_idx),
                      type=[0] * len(resource_name_by_idx),
                ),
                nativeSymbols=dict(),
                stringArray=string_list,
                sources=dict(),
                sourceLocationTable=dict(),
            ),
            threads=threads,
        ))

        with open("rakudo_profile.json", "w") as f:
            f.write(result)
        print(f"Data written to rakudo_profile.json: {len(result)} bytes")

execution_db : Connection | None = None

def ensure_execution_db() -> Connection:
    """Try to open an execution db sqlite file if $moar_rrdb_file is set."""
    global execution_db

    if execution_db is None:
        filename_maybe = gdb.parse_and_eval("$moar_rrdb_file")
        # print(f"Can I load db from $moar_rrdb_file? ({filename_maybe})")
        if str(filename_maybe) and str(filename_maybe) != "void" and Path(filename_maybe.string()).exists():
            import sqlite3
            print("Loading previous execution db from ", filename_maybe)
            execution_db = sqlite3.connect(filename_maybe.string(), autocommit=False)
            return execution_db
        else:
            print("No execution DB seems to exist. run `moar rrdb` first!")
            print("""(Or if you already have one, try `set $moar_rrdb_file = "/tmp/..."`)""")
            raise Exception("No execution db (from moar rrdb) seems to exist")

    return execution_db

def follow_fields(val : gdb.Value, fields : list[str]):
    try:
        for step in fields:
            val = val[step]
        return val
    except Exception as ex:
        print(f"error trying to get {fields} ", ex)
        raise

class MakeExecutionDatabaseCommand(gdb.Command):
    """Run execution from beginning to end, creating a database with interesting events.

    Only makes sense to run inside a "rr replay" session.

    Possible arguments:

      - trackgc      Record every object's old and new addresses when doing GC
      - deopt        Record whenever any frame hits a deopt one, deopt all, or lazy deopt.
      - stack        Take stack samples on every event.
    """

    allowedflags : Set[str] = {"trackgc", "deopt", "stack"}
    flags : Set[str]

    def __init__(self):
        super(MakeExecutionDatabaseCommand, self).__init__("moar rrdb", gdb.COMMAND_DATA)

    def complete(self, text, word):
        return [f for f in self.allowedflags if f not in text and (word is None or f.startswith(word))]

    stackrec : StackRecorder

    _created_breakpoints: list[gdb.Breakpoint] = []

    _gc_seq_num = None

    class _EXP:
        tid         = lambda _: lambda _, _2: ["tid", gdb.selected_thread().ptid_string]
        gdb_eval    = lambda _, sub, code, pytype: lambda _, _2: [sub, pytype(gdb.parse_and_eval(code))]
        local_var   = lambda _, sub, var_name, pytype: lambda frame, _: [sub, pytype(frame.read_var(var_name))]
        local_field = lambda _, sub, var_name, steps, pytype: lambda frame, _: [sub, pytype(follow_fields(frame.read_var(var_name), steps))]

        def local_fields(cls, var_name, steps, fields, pytype):
            def local_fields_impl(frame, _):
                val = frame.read_var(var_name)
                for step in steps:
                    val = val[step]
                res_keys = []
                res_vals = []
                for (colname, fieldname) in fields:
                    res_keys.append(colname)
                    try:
                        res_vals.append(pytype(val[fieldname]))
                    except gdb.MemoryError:
                        # print(f"Could not get value from field {fieldname} in {var_name}.{steps}")
                        res_vals.append(None)
                return res_keys, res_vals
            return local_fields_impl

    _prev_tl_entry = None
    _prev_thread = None
    _prev_thread_str = None

    _last_saved_event_time = -1

    def _insert_time_and_take_stack(self, rr_event, rr_tick, currthreadptid, rrcon: RrGdbConn):
        # If this is the firs time we see this rr_event time,
        if rr_event > self._last_saved_event_time:
            # Store the relative elapsed time in `event_times`
            rr_time  = rrcon.time()

            query = f'INSERT INTO event_times VALUES (?, ?, ?, ?)';
            self._db_cur.execute(query, (rr_event, rr_time, rr_tick, currthreadptid))
            self._last_saved_event_time = rr_event

            # If stack sampling was requested, take a sample and put it in
            # the stack recorder
            if "stack" in self.flags:
                # Record a stack into the stack tree
                to_insert = stack_for_stack_piece_inserter()
                try:
                    if to_insert:
                        self.stackrec.insert_stack_pieces(to_insert, rr_event=rr_event, tid=currthreadptid)
                except Exception as ex:
                    print("could not insert stack pieces: ", ex)
                    raise

    def _register_event_time_and_stack_only(self):
        rrcon = RrGdbConn()

        currthreadptid, rr_event, rr_tick = rrcon.ptid_event_tick()

        try:
            self._insert_time_and_take_stack(rr_event, rr_tick, currthreadptid, rrcon)

        finally:
            self._db_conn.commit()

    def _register_event_sql(self, table_name, column_expr, exprs):
        rrcon = RrGdbConn()

        currthreadptid, rr_event, rr_tick = rrcon.ptid_event_tick()

        frame = gdb.selected_frame()

        row = {}

        row["rr_tick"] = rr_tick
        row["rr_event"] = rr_event

        # helpers for "extras" handlers
        row["__frame"] = frame
        row["__currthreadptid"] = currthreadptid

        for expr_index, expr in enumerate(exprs):
            try:
                if isinstance(expr, list):
                    key, value = expr
                elif isinstance(expr, typing.Callable):
                    key, value = expr(frame, row)

                if isinstance(key, str):
                    row[key] = value
                else:
                    for i in range(len(key)):
                        row[key[i]] = value[i]
            except Exception as ex:
                print("when evaluating expr ", expr_index, " ", expr, " for table ", table_name)
                print(ex)

        query = f"INSERT INTO {table_name} VALUES ({column_expr});"
        try:
            # Insert row into the database
            try:
                self._db_cur.execute(query, row)
            except Exception as ex:
                print("row not entered", ex)

            self._insert_time_and_take_stack(rr_event, rr_tick, currthreadptid, rrcon)

        finally:
            self._db_conn.commit()

        return row

    def _register_object_movement(self):
        # If `trackgc` flag was turned on, store the before and after
        # addresses of any objects moved by the GC
        frame = gdb.selected_frame()

        rrcon = RrGdbConn()
        currthreadptid, rr_event, rr_tick = rrcon.ptid_event_tick()

        scan_ptr  = frame.read_var("tc")["nursery_fromspace"]
        limit_ptr = frame.read_var('limit')
        if int(limit_ptr) == int(scan_ptr):
            return

        nursery_start_addr = int(scan_ptr)

        nursery_memory = gdb.selected_inferior().read_memory(int(scan_ptr), int(limit_ptr) - int(scan_ptr))

        coll_typ = gdb.lookup_type("struct MVMCollectable")
        coll_typ_sz = coll_typ.sizeof
        forwarder_valid = int(gdb.lookup_symbol("MVM_CF_FORWARDER_VALID")[0].value())

        # scanned_items = 0
        # recorded_items = 0

        tuples = []
        froms = []
        tos = []

        min_to = math.inf
        max_to = 0

        largest_from_step = 0

        scan_idx = 0
        prev_scan_idx = 0
        while scan_idx < len(nursery_memory):
            item_mem = nursery_memory[scan_idx:scan_idx + coll_typ_sz]
            item = gdb.Value(item_mem, coll_typ)
            if int(item["flags2"]) & forwarder_valid:
                to_addr = int(item["sc_forward_u"]["forwarder"])
                # from_addr = nursery_start_addr + scan_idx

                step_size = scan_idx - prev_scan_idx
                froms.append(step_size)
                prev_scan_idx = scan_idx

                if step_size > largest_from_step:
                    largest_from_step = step_size

                tos.append(to_addr)
                if to_addr < min_to:
                    min_to = to_addr
                if to_addr > max_to:
                    max_to = to_addr

                # tuples.append((to_addr, from_addr))
                # recorded_items += 1

            # scanned_items += 1
            item_sz = int(item["size"])
            scan_idx += item_sz
            if not item_sz:
                raise ValueError("item with size 0 found?!")

        if tos:
            tos = [to - min_to for to in tos]
            array_bytesize = 8
            if max_to - min_to < 0xff:
                array_bytesize = 1
            elif max_to - min_to < 0xffff:
                array_bytesize = 2
            elif max_to - min_to < 0xffffffff:
                array_bytesize = 4

            tos_array = array(array_code_for_bytesize(array_bytesize), tos)

            array_bytesize = 8
            if largest_from_step < 0xff:
                array_bytesize = 1
            elif largest_from_step < 0xffff:
                array_bytesize = 2
            elif largest_from_step < 0xffffffff:
                array_bytesize = 4

            froms_array = array(array_code_for_bytesize(array_bytesize), froms)

            query = "INSERT INTO object_movements VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
            self._db_cur.execute(query, (
                 rr_event, rr_tick,
                 nursery_start_addr, int(limit_ptr),
                 froms_array.itemsize, froms_array,
                 min_to, max_to, tos_array.itemsize, tos_array
            ))

            self._db_conn.commit()

    def setup(self):
        import sqlite3

        self._last_saved_event_time = -1

        rrcon = RrGdbConn()

        def store_seq_num(ev):
            """After `process_worklist`, store the gc_sequence_number from
            the MVMInstance in the self._gc_seq_num.

            Also output a short status line every 20 gc runs."""
            prev = self._gc_seq_num
            self._gc_seq_num = int(ev["__frame"].read_var("tc")["instance"]["gc_seq_number"])

            if prev is not None and prev // 20 != self._gc_seq_num // 20:
                rr_event = ev["rr_event"]
                rr_time = rrcon.time()

                print(time.strftime("%H:%M:%S"), f" - reached gc run {self._gc_seq_num:4d} - time {rr_time:7.3f} event: {rr_event}")

        print(time.strftime("%H:%M:%S"), " - starting")

        self._db_file = tempfile.NamedTemporaryFile(prefix="moar-gdb-rrdb-", suffix=".sqlite3", delete=False)
        self._db_file.close()
        self._db_conn = sqlite3.connect(self._db_file.name, autocommit=False, timeout=30)
        self._db_cur = self._db_conn.cursor()

        self.stackrec = StackRecorder(gdb.inferiors()[0].pid, self._db_conn)

        def get_rr_tid(frame_, row):
            return ["rr_tid", rrcon.tid(row["__currthreadptid"])]

        print("")
        print("    sqlite3 file can be found at ", self._db_file.name)
        print("")

        self._db_cur.executescript("""
            create table compunits (
                tc integer,
                rr_tick integer,
                rr_event integer,
                data_addr integer
            );
            create table staticframes (
                rr_tick integer,
                rr_event integer,
                bytecode integer,
                compunit_data_addr integer,
                sf_addr integer,
                cuuid varchar,
                name varchar
            );
            create table spesh_bytecode (
                rr_tick integer,
                rr_event integer,
                sf_addr integer,
                bytecode_addr integer,
                size integer
            );
            create table gcs (
                tc integer,
                rr_tick integer,
                rr_event integer,
                gc_seq_number integer,
                is_full integer
            );
            create table gc_ends (
                rr_tick integer,
                rr_event integer
            );
            create table worklist_runs (
                tc integer,
                rr_tick integer,
                rr_event integer,
                nursery_alloc integer,
                nursery_alloc_limit integer,
                nursery_tospace integer,
                nursery_fromspace integer
            );
            create table continuation_events (
                tc integer,
                rr_tick integer,
                rr_event integer,
                callstack_region integer,
                is_slice boolean
            );
            create table nfa_runs (
                rr_tick integer,
                rr_event integer,
                offset integer,
                string integer
            );
            create table event_times (
                rr_event integer,
                rr_time float,
                rr_tick integer,
                rr_tid integer
            );
            create table threadcontexts (
                rr_event integer,
                tc integer,
                gdb_tid integer,
                rr_tid integer,
                exited_event integer
            );
            create table object_movements (
                rr_event integer,
                rr_tick integer,
                scan_start_addr integer,
                scan_end_addr integer,
                from_address_array_bytesize integer,
                from_address_steps blob,
                to_addr_offset integer,
                to_addr_maximum integer,
                to_address_array_bytesize integer,
                to_addresses blob
            );
            create table sc_code (
                sf_addr integer,
                sc_addr integer,
                idx integer
            );
            create table deopt (
                tc integer,
                rr_tick integer,
                rr_event integer,
                sf_addr integer,
                cur_op integer,
                cand_bc_addr integer,
                deopt_idx integer
            );

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

        self._created_breakpoints = []
        cbp = self._created_breakpoints

        cbp.append(SQLRecordingBreakpoint(self, "threadcontexts", "rr_event tc gdb_tid rr_tid exited_event", [
            EXP.local_var("tc", "tc", int),
            lambda _, row: ["gdb_tid", row["__currthreadptid"]],
            get_rr_tid,
            ["exited_event", None],
            ], "src/core/threads.c:thread_initial_invoke"))


        cbp.append(SQLRecordingBreakpoint(self, "gcs", "tc rr_tick rr_event gc_seq_number is_full", [
            EXP.local_var("tc", "tc", int),
            EXP.local_fields("tc", ["instance"],
                             [("gc_seq_number", "gc_seq_number"),
                              ("is_full", "gc_full_collect")], int),
            ], "run_gc"))

        gdb.execute("list finish_gc", False, True)
        finish_gc_orchestrator_signal_completed_lineno = gdb.execute("search uv_cond_broadcast(&tc->instance->cond_gc_completed);", False, True).split("\t")[0]

        cbp.append(SQLRecordingBreakpoint(self, "gc_ends", "rr_tick rr_event", [
            ], f"src/gc/orchestrate.c:{finish_gc_orchestrator_signal_completed_lineno}"))

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

        if "trackgc" in self.flags:
            cbp.append(ObjectMovementRecordingBreakpoint(self, "src/gc/collect.c:MVM_gc_collect_free_nursery_uncopied"))
        else:
            print("trackgc not passed. Will not record every object's old and new address.")

        if 'deopt' in self.flags:
            mbc_addr_str = gdb.execute("print/d &MAGIC_BYTECODE", False, True).splitlines()[0]
            magic_bytecode_addr = int(mbc_addr_str[mbc_addr_str.find(" = ")+3:])
            def addr_unless_its_magic_bytecode(v : gdb.Value):
                cur_op_addr = v.dereference()
                if int(cur_op_addr) == magic_bytecode_addr:
                    return None
                else:
                    return int(cur_op_addr)

            def tryint(v : gdb.Value):
                try:
                    return int(v)
                except gdb.MemoryError:
                    return None

            cbp.append(SQLRecordingBreakpoint(self, "deopt", "tc rr_tick rr_event sf_addr cand_bc_addr cur_op deopt_idx", [
                EXP.local_var("tc", "tc", int),
                EXP.local_field("sf_addr", "tc", ["cur_frame", "static_info"], int),
                EXP.local_field("cand_bc_addr", "tc", ["cur_frame", "spesh_cand", "body", "bytecode"], tryint),
                EXP.local_field("cur_op", "tc", ["interp_cur_op"], addr_unless_its_magic_bytecode),
                ["deopt_idx", None],
                ], "MVM_spesh_deopt_during_unwind"))

            cbp.append(SQLRecordingBreakpoint(self, "deopt", "tc rr_tick rr_event sf_addr cand_bc_addr cur_op deopt_idx", [
                EXP.local_var("tc", "tc", int),
                EXP.local_field("sf_addr", "tc", ["cur_frame", "static_info"], int),
                EXP.local_field("cand_bc_addr", "tc", ["cur_frame", "spesh_cand", "body", "bytecode"], tryint),
                EXP.local_field("cur_op", "tc", ["interp_cur_op"], addr_unless_its_magic_bytecode),
                EXP.local_var("deopt_idx", "deopt_idx", int),
                ], "MVM_spesh_deopt_one"))

            cbp.append(SQLRecordingBreakpoint(self, "deopt", "tc rr_tick rr_event sf_addr cand_bc_addr cur_op deopt_idx", [
                EXP.local_var("tc", "tc", int),
                EXP.local_field("sf_addr", "tc", ["cur_frame", "static_info"], int),
                EXP.local_field("cand_bc_addr", "tc", ["cur_frame", "spesh_cand", "body", "bytecode"], tryint),
                EXP.local_field("cur_op", "tc", ["interp_cur_op"], addr_unless_its_magic_bytecode),
                ["deopt_idx", None],
                ], "MVM_spesh_deopt_all"))
        else:
            print("deoptnot passed. Will not record calls deopt functions.")

        if "stack" in self.flags:
            cbp.append(EventTimeAndStackBreakpoint(self, "syscall_hook_internal"))

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
            EXP.local_var("candidate", "candidate", int),
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


        cbp.append(SQLRecordingBreakpoint(self, "continuation_events", "tc rr_tick rr_event callstack_top is_slice", [
            EXP.local_var("tc", "tc", int),
            EXP.local_field("callstack_top", "tc", ["stack_top"], int),
            lambda _, _2: ["is_slice", 0],
            ], "MVM_callstack_continuation_slice"))

        cbp.append(SQLRecordingBreakpoint(self, "continuation_events", "tc rr_tick rr_event callstack_top is_slice", [
            EXP.local_var("tc", "tc", int),
            EXP.local_var("callstack_top", "stack_top", int),
            lambda _, _2: ["is_slice", 1],
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

        last_event = 0
        records_this_event = 0
        def nfa_prereq():
            nonlocal last_event, records_this_event

            rr_event = rrcon.event()

            if last_event < rr_event or records_this_event < 10:
                records_this_event += 1
                last_event = rr_event
                return True

            return False

        nfa_bp = SQLRecordingBreakpointWithPrereq(self, 'nfa_runs', "rr_tick rr_event offset string", [
            EXP.local_var('offset', 'offset', int),
            EXP.local_var('string', 'target', int),
            ], 'nqp_nfa_run')
        nfa_bp.prereq = nfa_prereq

        cbp.append(nfa_bp)

    def teardown(self):
        print("Moar RRDB: Finished running. Tearing down breakpoints.")
        for bp in self._created_breakpoints:
            bp.delete()
        self._created_breakpoints = []


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

        self._created_breakpoints = []

        self.dont_repeat()
        try:
            with breakpoints_disabled():
                self.setup()

                self.run()
        finally:
            try:
                execution_db = self._db_conn
                gdb.set_convenience_variable("moar_rrdb_file", self._db_file.name)
            except Exception as e:
                print("Could not store the name of the rrdb file in convenience var :(", e)

            # try:
            if "stack" in self.flags:
                self.stackrec.dump_as_profile_file()

            # except Exception as ex:
                # print("Error while trying to dump stackrec profile file :(")
                # print(ex)

            print("")
            print("    sqlite3 file can be found at ", self._db_file.name)
            print("")


            self.teardown()


@dataclass(frozen=True)
class TimelineEvent:
    rr_event: int
    rr_tick: int

    def fmt(self):
        return "some event"

@dataclass(frozen=True)
class TimelineTextEvent(TimelineEvent):
    text: str

    def fmt(self):
        return self.text

@dataclass(frozen=True)
class YouAreHere(TimelineEvent):
    def fmt(self):
        return "  **  You Are Here  **  "

@dataclass(frozen=True)
class TimelineRange(TimelineEvent):
    end_rr_event: int
    end_rr_tick:  int | None

@dataclass(frozen=True)
class GcTimelineEvent(TimelineRange):
    gc_seq_num: int
    is_major: bool

    def fmt(self):
        return ("Major GC run " if self.is_major else "Minor GC run ") + str(self.gc_seq_num)

@dataclass(frozen=True)
class StackTimelineEvent(TimelineEvent):
    stack_depth: int

@dataclass(frozen=True)
class CallTimelineEvent(StackTimelineEvent):
    func_name: str
    kind: typing.Literal["invoke"] | typing.Literal["return"]

    def fmt(self):
        return self.func_name


class InvokeReturnTimelineRecordingBreakpoint(gdb.Breakpoint):
    hit_limit: int
    tick_limit: int | None
    conn: RrGdbConn

    def __init__(self, conn, starting_event, relevant_ptid, mtlc, tc, what, *args):
        super(InvokeReturnTimelineRecordingBreakpoint, self).__init__(*args)
        self.conn = conn
        self.relevant_ptid = relevant_ptid
        self.starting_event = starting_event
        self.mtlc = mtlc
        self.tc = tc
        self.what = what
        self.hit_limit = 0
        self.tick_limit = None

    def stop(self):
        try:
            return self.do_stop()
        except Exception as ex:
            print("in breakpoint stop impl: ", ex)
        return True

    def do_stop(self):
        currthreadptid = self.conn.ptid()

        self.hit_limit -= 1
        if self.hit_limit == 0:
            return True

        if currthreadptid != self.relevant_ptid:
            print(f"Left current thread for breakpoint {self.location}")
            return True

        rr_event = self.conn.event(currthreadptid)

        if not (self.starting_event - 2 <= rr_event <= self.starting_event + 2):
            print(f"Left current event for breakpoint {self.location}: {self.starting_event} vs {rr_event}")
            return True

        rr_tick = self.conn.tick(currthreadptid)

        if self.tick_limit is not None and rr_tick > self.tick_limit:
            print(f"hit tick limit: {self.tick_limit} <= {rr_tick}")
            return True

        stack_depth = 0

        funcname = None

        if self.what == "invoke":
            stack_depth += 1
            invoked_sfb = gdb.selected_frame().read_var("code")["body"]["sf"]["body"]

            funcname = mvmstr_to_str(invoked_sfb["name"])
            if funcname == "":
                funcname = f"({mvmstr_to_str(invoked_sfb["cuuid"])})"

        cur_frame = MoarStackFrame.from_tc(self.tc)
        if cur_frame is not None:
            if funcname is None:
                funcname = cur_frame.name
                if funcname == "":
                    funcname = f"({cur_frame.cuuid})"

            while cur_frame is not None:
                cur_frame = cur_frame.caller
                stack_depth += 1
        else:
            if funcname is None:
                funcname = "???"

        self.mtlc.register_timeline_breakpoint(rr_event, rr_tick, self.what, funcname, stack_depth)

        return False


class TimelineWriter:
    events: list[TimelineEvent]

    current_stack_depth: int
    event_of_line: list[TimelineEvent]
    times_of_events: dict
    limit: int

    def __init__(self, events, times_of_events, limit):
        self.events = sorted(events, key=lambda e: (e.rr_event, e.rr_tick))
        self.times_of_events = times_of_events
        self.limit = limit

    def draw_stack(self, abbrev_stack_depth: int | float, prev_stack_depth: int, new_stack_depth: int, kind: str):
        res = []
        if isinstance(abbrev_stack_depth, int):
            doubles = abbrev_stack_depth // 2
            res += ["║"] * doubles
            res += ["┊"] * (abbrev_stack_depth - doubles * 2)
            prev_stack_depth -= abbrev_stack_depth
            new_stack_depth  -= abbrev_stack_depth
        res += [" "]
        shared_stack_size = min(prev_stack_depth, new_stack_depth)
        res += ["│ "] * (shared_stack_size)

        # print(f"draw stack: {prev_stack_depth=} {new_stack_depth=} {shared_stack_size=} {kind=}")

        if kind == "return":
            new_stack_depth += 1

        if new_stack_depth > prev_stack_depth:
            res += ["├"] + ["─┬"] * (new_stack_depth - shared_stack_size - 1)
        elif new_stack_depth < prev_stack_depth:
            res += ["├"] + ["─┴"] * (prev_stack_depth - shared_stack_size - 1)
        else:
            res += ["├"]

        if kind == "invoke":
            res += ["─┬"]
            effective_stack_diff = 1
        elif kind == "return":
            res += ["─╯"]
            effective_stack_diff = -1
        elif kind == "nothing":
            res += ["─┄ "]
            effective_stack_diff = 0

        return ("".join(res), (abbrev_stack_depth + new_stack_depth) + effective_stack_diff)

    def get_time_of_event(self, event):
        return self.times_of_events.get(event, math.nan)

    def stringify(self):
        output_lines = []
        def l(ev, *text):
            output_lines.append([ev, " ".join(text)])

        lines_before_you_are_here = []

        self.current_stack_depth = 0
        self.event_of_line = []

        calls_before_yah = []
        calls_after_yah  = []
        invoke_search_borders = []
        for ev in self.events:
            if isinstance(ev, CallTimelineEvent):
                calls_after_yah.append(ev)
            elif isinstance(ev, TimelineTextEvent):
                if ev.text == "Invoke search border":
                    invoke_search_borders.append(ev)
            elif isinstance(ev, YouAreHere):
                calls_before_yah = calls_after_yah
                calls_after_yah = []

        to_remove = []
        if len(calls_before_yah) > self.limit:
            to_remove.extend(calls_before_yah[:-self.limit])
            border = invoke_search_borders[0]
            self.events.append(TimelineTextEvent(border.rr_event, border.rr_tick + 1, f"{len(to_remove)} events hidden"))

        if len(calls_after_yah) > self.limit:
            to_remove_here = calls_after_yah[self.limit:]
            border = invoke_search_borders[1]
            self.events.append(TimelineTextEvent(border.rr_event, border.rr_tick - 1, f"{len(to_remove_here)} events hidden"))
            to_remove.extend(to_remove_here)

        if to_remove:
            self.events = [line for line in self.events if line not in to_remove]

        # after putting the "events hidden" messages, re-sort
        self.events = sorted(self.events, key=lambda e: (e.rr_event, e.rr_tick))

        self.you_are_here = [e for e in self.events if isinstance(e, YouAreHere)][0]
        now_event = self.you_are_here.rr_event
        now_tick  = self.you_are_here.rr_tick

        groups = defaultdict(list)
        shallowest_stack_depth_per_group = defaultdict(lambda: math.inf)

        lowest_tick = math.inf
        highest_tick = 0

        for ev in self.events:
            groups[ev.rr_event].append(ev)
            try:
                if ev.stack_depth < shallowest_stack_depth_per_group[ev.rr_event]:
                    shallowest_stack_depth_per_group[ev.rr_event] = ev.stack_depth
            except AttributeError:
                pass
            lowest_tick  = min(lowest_tick,  ev.rr_tick)
            highest_tick = max(highest_tick, ev.rr_tick)

        # XXX: Can currently not deal with overlapping timeline range events,
        #      but the only range event we have right now is GC runs, which
        #      cannot overlap.

        tick_field_len = len(str(highest_tick))

        prev_group = (None, None)


        for event, group in groups.items():
            shallowest = shallowest_stack_depth_per_group[event]

            prev_stack_depth: int | None  = None

            relpos = "before"
            if event == now_event:
                relpos = "current"
            elif event > now_event:
                relpos = "after"

            time = self.get_time_of_event(event)
            if math.isnan(time):
                time = "unknown time"
            else:
                time = f"{time}s"
            l(None, f"Event {event} at {time}")

            any_stacks_in_group = bool([e for e in group if isinstance(e, StackTimelineEvent)])

            prev_line_fmt = ""

            prev_ev = None
            for ev in group:
                if prev_ev == ev:
                    continue
                try:
                    stack_depth: int = ev.stack_depth

                    if prev_stack_depth is None:
                        prev_stack_depth = stack_depth

                    (stack_bit, prev_stack_depth) = self.draw_stack(shallowest - 1, prev_stack_depth, stack_depth, ev.kind)
                except AttributeError as ex:
                    if event in shallowest_stack_depth_per_group and prev_stack_depth is not None:
                        (stack_bit, prev_stack_depth) = self.draw_stack(shallowest - 1, prev_stack_depth, prev_stack_depth, "nothing")
                    else:
                        stack_bit = ""

                l(ev, "  ", (f"%{tick_field_len}d" % ev.rr_tick), "    ", stack_bit, ev.fmt())

                if isinstance(ev, YouAreHere):
                    lines_before_you_are_here = output_lines
                    output_lines = []

                prev_ev = ev

            prev_group = (event, group)

        return "\n".join([e[1] for e in lines_before_you_are_here + output_lines])


class MoarTimelineCommand(gdb.Command):
    """Show recent and upcoming events from moar rrdb's execution db

    You can pass a number as the only argument to get more or fewer lines
    around the current time, otherwise you get up to 18.."""

    flags : Set[str]
    found_entries: list[CallTimelineEvent] = []
    time_of_event: dict = {}

    def __init__(self):
        super(MoarTimelineCommand, self).__init__("moar timeline", gdb.COMMAND_STATUS)

    def register_timeline_breakpoint(self, event, tick, thing, func, depth):
        # print(f"registering timeline event: {event=} {tick=} {thing=} {func=} {depth=}")
        if event not in self.time_of_event:
            self.time_of_event[event] = RrGdbConn().time()
        self.found_entries.append(CallTimelineEvent(event, tick, depth, func, thing))

    def invoke(self, argument, from_tty):
        pagination_before = gdb.execute("show pagination", False, True) == "State of pagination is on.\n"
        gdb.execute("set pagination off", False, False)
        try:
            self.do_invoke(argument, from_tty)
        finally:
            if pagination_before:
                gdb.execute("set pagination on", False, True)

    def do_invoke(self, argument : str, from_tty):
        if argument and argument.isdecimal():
            event_limit = int(argument)
            print(f"using {argument=} as the limit ({event_limit=})")
        else:
            event_limit = 18

        rrcon = RrGdbConn()

        tc = find_tc()

        execution_db = ensure_execution_db()

        self.found_entries = []

        try:
            cur = execution_db.cursor()
            cur.execute("select rr_event, rr_time from event_times;")
            self.time_of_event = {e[0]: e[1] for e in cur.fetchall()}
        except:
            raise Exception("Could not retrieve data from execution db?!")

        currthreadptid, rr_event, rr_tick = rrcon.ptid_event_tick()
        rr_time  = rrcon.time()

        chkp_res = gdb.execute("checkpoint", False, True)
        chkp_id = chkp_res[len("Checkpoint "):chkp_res.find(" at ")]

        tick_range_checked = (0, 0)
        event_range_checked = (0, 0)

        with breakpoints_disabled():
            return_bp = InvokeReturnTimelineRecordingBreakpoint(rrcon, rr_event, currthreadptid, self, tc, "return", "MVM_frame_try_return")
            invoke_bp = InvokeReturnTimelineRecordingBreakpoint(rrcon, rr_event, currthreadptid, self, tc, "invoke", "MVM_frame_dispatch")

            try:
                print("skipping backwards to collect invokes/returns")
                found_stuff = 0
                seek_offset = 100 * event_limit

                return_bp.tick_limit = rr_tick
                invoke_bp.tick_limit = rr_tick

                while found_stuff < event_limit and seek_offset < 700000:
                    seek_offset *= 1.5
                    seek_offset = int(seek_offset)
                    seek_tick = rr_tick - seek_offset

                    print(f"Hopping backwards to {seek_offset} ticks ago with limit of {return_bp.tick_limit}; found {len(self.found_entries)} entries so far")
                    gdb.execute(f"seek-ticks {seek_tick}")
                    tick_range_checked = (seek_tick, 0)

                    event_range_checked = (rrcon.event(), 0)

                    currthreadptid, stopped_rr_event, stopped_rr_tick = rrcon.ptid_event_tick()

                    if stopped_rr_event != rr_event:
                        print(f"seeked to {seek_offset} ticks earlier, but went to wrong event")

                    gdb.execute("continue")

                    found_stuff = len(self.found_entries)

                    return_bp.tick_limit = rr_tick - seek_offset
                    invoke_bp.tick_limit = rr_tick - seek_offset

                gdb.execute(f"restart {chkp_id}")

                return_bp.hit_limit = event_limit * 2
                invoke_bp.hit_limit = event_limit * 2
                return_bp.tick_limit = None
                invoke_bp.tick_limit = None

                print("Running forwards to collect invokes/returns")
                gdb.execute("continue")

                (currthreadptid, end_event, end_tick) = rrcon.ptid_event_tick()
                event_range_checked = (event_range_checked[0], end_event)
                tick_range_checked = (tick_range_checked[0], end_tick)

            except Exception as ex:
                print("Error trying to get invokes and returns for current event: ", ex)
            finally:
                return_bp.delete()
                invoke_bp.delete()
                print("Returning to checkpoint before command started")
                gdb.execute(f"restart {chkp_id}")
                gdb.execute(f"delete checkpoint {chkp_id}")

        # found_entries = [e for e in self.found_entries if e.rr_event == rr_event]
        found_entries = sorted(self.found_entries, key=lambda e: (e.rr_event, e.rr_tick))

        try:
            cur = execution_db.cursor()

            gcs_query = """
                select
                   igcs.earliest,
                   igcs.earliest_same_tc_tick,
                   igcs.latest,
                   igcs.gc_seq_number,
                   igcs.is_full
                from
                    (select
                        abs(min(rr_event) - :now_rr_event) as absdiff,
                        min(rr_event) as earliest,
                        max(rr_event) as latest,
                        min(rr_tick) filter (where tc = :tc) as earliest_same_tc_tick,
                        gc_seq_number,
                        is_full
                    from gcs
                    where is_full = :want_full
                    group by gc_seq_number
                    order by 1 asc) igcs
                limit :limit;
            """

            continuation_query = """
            select
              abs(min(start_rr_event - :now_rr_event, end_rr_event - :now_rr_event)) as eventdiff,
              start_tc,
              end_tc,
              start_rr_event,
              end_rr_event,
              start_tick,
              end_tick
            from
              (SELECT
                st.tc AS start_tc,
                ed.tc AS end_tc,
                st.rr_event AS start_rr_event,
                ed.rr_event AS end_rr_event,
                st.rr_tick AS start_tick,
                ed.rr_tick AS end_tick,
                rank() over w as rank
              FROM
                continuation_events st
                INNER JOIN continuation_events ed
                  ON st.callstack_region = ed.callstack_region
                  AND (
                      st.rr_event < ed.rr_event
                      OR (
                        st.rr_event = ed.rr_event
                        AND st.tc = ed.tc
                        AND st.rr_tick < ed.rr_tick
                      )
                  )
              WHERE
                st.is_slice = 0
                AND ed.is_slice = 1
              WINDOW
                w AS (
                    PARTITION BY
                      st.rr_event,
                      st.rr_tick,
                      st.callstack_region,
                      ed.callstack_region
                    ORDER BY
                      st.rr_event ASC,
                      st.rr_tick ASC,
                      ed.rr_event ASC,
                      ed.rr_tick ASC
                )
              )
              where rank = 1
                and (start_tc = :now_tc
                     or end_tc = :now_tc)
              order by 1,2
              limit :limit;
            """

                # and start_rr_event = :now_rr_event

            timeline_events = []
            for (want_full, lim) in [(0, 4), (1, 3)]:
                nearby_gcs = cur.execute(gcs_query, dict(
                     now_rr_event=rr_event,
                     tc=int(find_tc()),
                     want_full=want_full,
                     limit=lim)).fetchall()
                nearby_gcs = [GcTimelineEvent(e[0], e[1], e[2], None, e[3], bool(e[4])) for e in nearby_gcs]
                timeline_events.extend(nearby_gcs)

            # nearby_continuation_events = cur.execute(continuation_query, dict(
            #     now_rr_event=rr_event,
            #     now_rr_tick=rr_tick,
            #     now_tc=int(tc),
            #     limit=limit
            # ))
            # cont_events = [e for e in nearby_continuation_events.fetchall()]
            # cont_events = sorted(cont_events, key=lambda e: (e[2], e[3]))

            timeline_events = sorted(timeline_events, key=lambda e: (e.rr_event, e.rr_tick))

            # for new impl of writer
            full_timeline_events : list[TimelineEvent] = []
            full_timeline_events.extend(timeline_events)
            full_timeline_events.extend((TimelineTextEvent(event, tick, "Invoke search border") for (event, tick) in zip(event_range_checked, tick_range_checked)))
            full_timeline_events.extend(found_entries)
            full_timeline_events.append(YouAreHere(rr_event, rr_tick))

            # harmonize types (since the python plugin is reloaded multiple
            # times while jumping back and forth in time)
            full_timeline_events = [globals()[type(ev).__name__](**ev.__dict__) for ev in full_timeline_events]

            print(TimelineWriter(full_timeline_events, self.time_of_event, event_limit).stringify())

        except Exception as ex:
            print("Failed to get timeline stuff: ", ex)
            raise

        finally:
            cur.close()

#     _             _   _                                                     _
#    | |__  __ _ __| |_| |_ _ _ __ _ __ ___   __ ___ _ __  _ __  __ _ _ _  __| |___
#    | '_ \/ _` / _| / /  _| '_/ _` / _/ -_) / _/ _ \ '  \| '  \/ _` | ' \/ _` (_-<
#    |_.__/\__,_\__|_\_\\__|_| \__,_\__\___| \__\___/_|_|_|_|_|_\__,_|_||_\__,_/__/
#

def can_read(val: gdb.Value):
    """Attempt to read memory at the value's target address.

    Returns True if it's valid, False otherwise."""
    try:
        val.cast(uint32p_t).dereference()
    except gdb.MemoryError:
        return False
    return True

def can_read_path(val: gdb.Value, *path: str):
    """Given a value, try to follow a path of field accesses in a row.

    Returns True if the final step can be followed, False if any step
    was not a valid memory access."""
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
    """Check if a gdb.Value of type MVMThreadContext * is plausible as
    an actual MVMThreadContext, based on contents and its pointers.

    Returns a plausibility score between 0 (plausible) and negative
    infinity (definitely not plausible)"""
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
            try:
                if int(tc["thread_obj"]["body"]["tc"]) != int(tc):
                    deduct(250)
                if not (0 <= int(tc["thread_obj"]["body"]["stage"]) <= 6):
                    deduct(100)
            except gdb.MemoryError:
                deduct(400)

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
    """For the selected frame, go through the stack as well as thread
    local storage to find the most plausible value for the thread's TC"""
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
    """Mirrors moar's MVM_frame_effective_bytecode."""
    spesh_cand = frame["spesh_cand"]
    if int(spesh_cand) != 0:
        if int(spesh_cand["body"]["jitcode"]) != 0:
            return spesh_cand["body"]["jitcode"]["bytecode"]
        return spesh_cand["body"]["bytecode"]
    return frame["static_info"]["body"]["bytecode"]

def decode_utf8_in_stringheap(val, entrysize):
    return val.string("utf-8", "backslashreplace", entrysize)

def string_from_cu(cu, index):
    """Like MVM_cu_string, grab the string at the index from the
    given MVMCompUnit."""

    cub = cu["body"]
    strp = cub["strings"][index]

    if int(strp) == 0:
        try:
            # not decoded yet, have to do it in here

            # can hopefully at least find it quickly with the fast table?
            fast_table_top = cub["string_heap_fast_table_top"]

            # TODO can we safely get this with gdb from dwarf data?
            bin = index // 16

            fast_table = cub["string_heap_fast_table"]

            # dont try to read outside the fast table!
            if bin > int(fast_table_top):
                bin = int(fast_table_top)

            strheap = cub["string_heap_start"] + fast_table[bin]
            found_idx = bin * 16

            while found_idx < index:
                entrysize = int(int(strheap.cast(uint32p_t).dereference()) // 2)
                if entrysize & 3:
                    entrysize += 4 - (entrysize & 3)
                strheap = strheap + (int(entrysize) + 4)
                found_idx += 1

            size_and_flag = int(strheap.cast(uint32p_t).dereference())
            entrysize = size_and_flag // 2

            data = decode_utf8_in_stringheap(strheap + 4, entrysize)
            return data
        except gdb.MemoryError as ex:
            return "<could not get string: " + str(ex) + ">"
    else:
        try:
            return mvmstr_to_str(strp.dereference())
        except gdb.MemoryError as ex:
            return "<could not get string: " + str(ex) + ">"

def resolve_annotation(sfb, offset, str_cache = None):
    """Mirrors moar's MVM_bytecode_resolve_annotation"""
    num_anno = sfb["num_annotations"]
    if num_anno == 0:
        return (None, None)

    if not (offset >= 0 and offset < sfb["bytecode_size"]):
        return (None, None)

    # read entire annotation data ahead of time
    entire_buf = gdb.selected_inferior().read_memory(sfb["annotations_data"], 12 * num_anno)

    ann_offs = 0

    p_cur_anno = None
    for annot in struct.iter_unpack("iii", entire_buf):
        ann_offs = annot[0]

        if ann_offs > offset:
            break

        p_cur_anno = annot

    if p_cur_anno is None:
        return (None, None)

    fnshi = p_cur_anno[1]
    ln    = p_cur_anno[2]

    cu = sfb["cu"]
    if str_cache is not None and (int(cu), fnshi) in str_cache:
        fn = str_cache[(int(cu), fnshi)]
    else:
        fn = string_from_cu(cu, fnshi)
        if str_cache is not None:
            str_cache[(int(cu), fnshi)] = fn

    return (fn, ln)

def parse_callsite(cs : gdb.Value):
    """Given an MVMCallsite*, return information about the arguments:

    [name if present, otherwise type name (obj/int/num/str/uint),
    function to turn a MVMRegister* into the right value,
    [name of argument or None,
     type name,
     flag value,
     flag value masked to arg type]
    ]"""

    num_flags = cs["flag_count"]
    flags = cs["arg_flags"]

    args = []
    name_idx = 0

    arg_names = cs["arg_names"]

    for i in range(num_flags):
        flagvar = int(flags[i])
        arg_type_masked = flagvar & 143
        arg_named_flat_masked = flagvar & 159

        namestr = None

        if flagvar & 32: # MVM_CALLSITE_ARG_NAMED
            namestr = mvmstr_to_str(arg_names[name_idx])
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

        self._outer = self.ptr["outer"]
        self._caller = self.ptr["caller"]
        self._static_info_body = self.ptr["static_info"]["body"]

        self._params = self.ptr["params"]
        self._arg_info = self._params["arg_info"]

        self._outer_obj = None
        self._caller_obj = None

    @classmethod
    def from_tc(cls, tc : gdb.Value | None = None) -> MoarStackFrame | None:
        """Given an MVMThreadContext *, create a MoarStackFrame from
        its cur_frame"""
        if tc is None:
            tc = find_tc()

        if int(tc["cur_frame"]) == 0:
            return None

        return MoarStackFrame(tc["cur_frame"], tc, MoarStackFrame.Relation.CUR_FRAME)

    @property
    def caller(self):
        if self._caller_obj:
            return self._caller_obj

        if int(self._caller) != 0:
            self._caller_obj = MoarStackFrame(self._caller, self, MoarStackFrame.Relation.CALLEE)
            return self._caller_obj
        else:
            return None

    @property
    def outer(self):
        if self._outer_obj:
            return self._outer_obj

        if int(self._outer) != 0:
            self._outer_obj = MoarStackFrame(self._outer, self, MoarStackFrame.Relation.OUTER)
            return self._outer_obj
        else:
            return None

    @property
    def name(self):
        sfb = self._static_info_body
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
        sfb = self._static_info_body
        bc = frame_effective_bytecode(self.ptr)
        return int(self.cur_op) - int(bc)

    @property
    def params(self):
        return self._params

    @property
    def param_vals(self):
        callsite = self._arg_info["callsite"]
        csinfo = parse_callsite(callsite)

        source = self._arg_info["source"]
        map = self._arg_info["map"]

        return [
            source[int(map[i])] for i in range(len(csinfo))]

    @property
    def cuuid(self):
        return mvmstr_to_str(self._static_info_body["cuuid"])

    @property
    def cufile(self):
        return mvmstr_to_str(self._static_info_body["cu"]["body"]["filename"])

    def resolve_annotation(self, offs : int | None = None, str_cache = None):
        if offs is None:
            offs = self.bytecode_offs

        sfb = self._static_info_body

        return resolve_annotation(sfb, offs, str_cache)

    def lexicals(self):
        sfb = self._static_info_body
        num_lexicals = sfb["num_lexicals"]

        # TODO probably not so important to have debug locals here
        # debug_locals = None
        # if int(sfb["instrumentation"]) != 0:
        #     debug_locals = sfb["instrumentation"]["debug_locals"]

        names_list = sfb["lexical_names_list"]
        types = sfb["lexical_types"]
        env = self.ptr["env"]

        for i in range(num_lexicals):
            name = names_list[i]
            typ  = type_to_register_member[int(types[i])]
            val  = env[i][typ]

            yield (mvmstr_to_str(name), typ, val)


def stack_for_stack_piece_inserter() -> Sequence[tuple[gdb.Value, gdb.Value]]:
    bits = []
    cur_frame : MoarStackFrame = MoarStackFrame.from_tc()

    while cur_frame is not None:
        bits.append((cur_frame.cur_op, cur_frame._static_info_body))
        cur_frame = cur_frame.caller

    return bits

def stringify_object_arg(objptr, str_cache = None):
    """Given an MVMObject *, try to get a better string representation than just the address.

    Differentiates between STables, Frames, Type Objects, and Instances.

    For P6str instances, gets the string inside."""

    stringified = None
    typename    = None

    if str_cache is not None and (int(objptr), "arg") in str_cache:
        stringified = str_cache[(int(objptr), "arg")]

    else:
        flags1 = int(objptr["header"]["flags1"])
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
            if str_cache is not None and int(objptr["st"]) in str_cache:
                reprname = str_cache[int(objptr["st"])]
            else:
                reprname = objptr["st"]["REPR"]["name"].string()
                if str_cache is not None:
                    str_cache[int(objptr["st"])] = reprname

            if is_concrete and reprname == "P6str":
                #print("casting to p6str? before:")
                #print(repr(subpart), repr(subpart.type))
                objptr = objptr.cast(stoogep_t)["data"].cast(mvmstrp_t)
                #print(repr(subpart), repr(subpart.type))
                #print("trying to mvmstr_to_str this:", repr(mvmstr_to_str(subpart)))
                spk = (int(objptr), "arg")
                if str_cache is not None and spk in str_cache:
                    stringified = str_cache[spk]
                else:
                    stringified = "((MVMString *)" + hex(int(objptr)) + ")=" + repr(mvmstr_to_str(objptr, truncate=128))
                    if str_cache is not None:
                        str_cache[spk] = stringified

            elif int(objptr["st"]["debug_name"]) != 0:
                typename = reprname + "#" + objptr["st"]["debug_name"].string()
                if is_concrete:
                    typename = typename + ".new"

        if str_cache is not None:
            str_cache[(int(objptr), "arg")] = stringified

    return (stringified, typename)

def extract_moar_stack_frame_args(cur_frame, str_cache = None):
    """From a MoarStackFrame, parse the callsite and get a tuple with the
    gdb.Value and a string with information for each of the arguments"""
    callsite = cur_frame.params["arg_info"]["callsite"]
    if str_cache is not None and int(callsite) in str_cache:
        csinfo = str_cache[int(callsite)]
    else:
        csinfo = parse_callsite(callsite)
        if str_cache is not None:
            str_cache[int(callsite)] = csinfo

    source = cur_frame._arg_info["source"]
    map = cur_frame._arg_info["map"]

    param_vals = [source[int(map[i])] for i in range(len(csinfo))]

    infoparts = []

    for i, csi in enumerate(csinfo):
        # info = csinfo[i][0]
        subpart  = csi[1](param_vals[i])
        typename = csi[2][1]
        argname  = csi[2][0]

        string_of_subpart = None

        #print("adding arg of", typename, argname, repr(subpart))
        if typename == "obj":
            (string_of_subpart, found_typename) = stringify_object_arg(subpart, str_cache)

        if string_of_subpart is None:
            string_of_subpart = gdb.printing.make_visualizer(subpart).to_string()
        if string_of_subpart is None:
            string_of_subpart = "<???>"

        if found_typename is not None:
            typename = found_typename

        infoparts.append([subpart,
            (argname + "=" if argname else "")
            + typename
            + "("
            + string_of_subpart
            + ")"])

    return infoparts

class MoarBtCommands(gdb.Command):
    """Commands to look at a moar-level backtrace."""

    def __init__(self):
        super(MoarBtCommands, self).__init__("moar bt", gdb.COMMAND_STACK, prefix=True)

    # Uncomment this version of invoke and rename the below one to invoke_
    # to cProfile running it.

    # def invoke(self, argument, from_tty):
    #     import cProfile
    #     pr = cProfile.Profile()
    #     with pr:
    #         self.invoke_(argument, from_tty)

    #     pr.print_stats(1)

    def invoke(self, argument, from_tty):
        flags = set(argument.split(" )"))

        str_cache = {}

        tc = find_tc()
        cur_frame : MoarStackFrame = MoarStackFrame.from_tc(tc)

        stack_idx = 0

        output = []

        while cur_frame is not None:
            static_info = cur_frame.ptr["static_info"]
            if int(static_info) in str_cache:
                name = str_cache[int(static_info)]
            else:
                name = cur_frame.name
                str_cache[int(static_info)] = name

            fn, ln = cur_frame.resolve_annotation(None, str_cache)

            if "noargs" not in flags:
                infoparts = extract_moar_stack_frame_args(cur_frame, str_cache)

                csinfo_str = "args=(" + ", ".join(p[1] for p in infoparts) + ")"
            else:
                csinfo_str = ""

            # Instead of just outputting "'' at <unknown>:1", make an attempt
            # to get at least something to differentiate one thing from another
            outer_str = ""
            if not name and (fn is None or ln is None):
                outer_frame = cur_frame.outer
                outer_fn, outer_ln = outer_frame.resolve_annotation(0, str_cache)
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

                fn, ln = cur_frame.resolve_annotation(offs=0, str_cache=str_cache)
                if fn is not None and ln is not None:
                    locstr = f"somewhere after {fn}:{ln}"

            if outer_str:
                locstr = locstr + " " + outer_str

            output.append(f"#{stack_idx:3d} {str(cur_frame.ptr):20s} {name} {csinfo_str} {locstr}")

            cur_frame = cur_frame.caller
            stack_idx += 1

        print("\n".join(output))


def do_single_frame_command_stuff(cur_frame : MoarStackFrame, stack_idx = None):
    """Given a MoarStackFrame, output a load of information about it
    to the terminal."""

    fn, ln = cur_frame.resolve_annotation()

    name = cur_frame.name
    name = "''" if name == "" else name

    loc = ""

    if fn is None or ln is None:
        loc = f"<unknown>:1"
    else:
        loc = f"{fn} : {ln}"

    if stack_idx is not None:
        print(f"Stack Frame #{stack_idx}")

    print(f"$mframe = (MVMFrame *) {cur_frame.ptr}")
    print("")
    print(f"    Name: {name}    ({loc})")
    print("")
    print(f"Bytecode file: {cur_frame.cufile} ; cuuid {cur_frame.cuuid}")
    print("")

    need_space = 0
    if cur_frame.caller is not None:
        cname = None
        try:
            cname = cur_frame.caller.name
            if cname:
                cname = f"({cname})"
            else:
                cname = ""
        except:
            cname = "(error getting caller's name)"
        print(f"Caller: $mframe->caller = {cur_frame.caller.ptr} {cname}")
        need_space = 1

    if cur_frame.outer is not None:
        try:
            oname = cur_frame.caller.name
            if oname:
                oname = f"({oname})"
            else:
                oname = ""
        except:
            oname = "(error getting caller's name)"
        print(f"Outer:  $mframe->outer  = {cur_frame.outer.ptr} {oname}")
        need_space = 1

    if need_space:
        print("")

    print("Arguments:")
    infoparts = []
    try:
        infoparts = extract_moar_stack_frame_args(cur_frame)

        for index, (value, info) in enumerate(infoparts):
            varname = f"margs_{index}"
            gdb.set_convenience_variable(varname, value)
            print(f"  $margs_{index:<2d} = {info}")
    except Exception as ex:
        print("Error trying to get arguments: ", ex)
    print("")

    try:
        lexes = list(cur_frame.lexicals())
        if lexes:
            print("Lexicals:")
        for index, (name, typecode, value) in enumerate(lexes):
            orig_typecode = typecode
            if int(value) != 0:
                if typecode == "o":
                    (stringified, typename) = stringify_object_arg(value)
                    if stringified is not None:
                        value = stringified
                    else:
                        value = gdb.printing.make_visualizer(value).to_string()

                    if typename is not None:
                        typecode = typename

                if isinstance(value, gdb.Value):
                    value = gdb.printing.make_visualizer(value).to_string()

                print(f"    $mframe->env[{index:2d}].{orig_typecode}: {name}: ({typecode}) = {value}")
            else:
                print(f"    {name}: ({typecode}) = <unset>")
    except Exception as ex:
        print("Error trying to get lexicals: ", ex)

    print("")

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
    # print(f"var $mframe set to {cur_frame.ptr}")

    gdb.set_convenience_variable("margs_cnt", len(infoparts))
    print(f"var $margs_cnt set to {len(infoparts)}")

    try:
        start = len(infoparts)
        end = prev_margs_cnt
        for i in range(start, end):
            varname = f"margs_{i}"
            gdb.set_convenience_variable(varname, unset_val)
        if start < end - 1:
            print(f"cleaned up obsolete $margs_ vals from {start} through {end - 1}")
        if start == end - 1:
            print(f"cleaned up obsolete $margs_{end - 1}")
    except:
        print("could not unset obsolete margs convenience vars")


class MoarFrameCommands(gdb.Command):
    """Get all details of one stack frame on the moarvm stack.

    Pass a pointer starting with * to use inspect a frame explicitly, pass no
    argument to get the current frame, or a number to get the nth frame on
    the stack."""
    def __init__(self):
        super(MoarFrameCommands, self).__init__("moar frame", gdb.COMMAND_STACK, prefix=True)

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


class MoarFrameUpCommand(gdb.Command):
    """Get all details of the current moar stack frame's caller."""
    def __init__(self):
        super(MoarFrameUpCommand, self).__init__("moar frame up", gdb.COMMAND_STACK)

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

class MoarFrameOutCommand(gdb.Command):
    """Get all details of the current moar stack frame's outer."""
    def __init__(self):
        super(MoarFrameOutCommand, self).__init__("moar frame out", gdb.COMMAND_STACK)

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

# currently unused and probably unoperational
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

    commands.append(StrHashCommand())

    commands.append(MakeExecutionDatabaseCommand())
    print("command moar rrdb registered")

    commands.append(MoarTimelineCommand())
    print("command moar timeline registered")

    commands.append(MoarBreakCommands())
    commands.append(MoarBreakInterpRunCommands())
    commands.append(MoarBreakExceptionAdhoc())
    print("moar break commands registered")

    commands.append(MoarBtCommands())
    print("moar bt command registered")

    commands.append(MoarFrameCommands())
    commands.append(MoarFrameUpCommand())
    commands.append(MoarFrameOutCommand())
    print("moar frame commands registered")

    commands.append(MoarTCCommand())
    print("moar TC commands registered")

def say_hello():
    try:
        if gdb.parse_and_eval('$moarvm_gdb_plugin_banner_shown') == 1:
            print("MoarVM GDB Plugin re-loaded")
            return
    except:
        pass

    print(r"""
 __  __                __     ____  __    ____ ____  ____
|  \/  | ___   __ _ _ _\ \   / /  \/  |  / ___|  _ \| __ )
| |\/| |/ _ \ / _` | '__\ \ / /| |\/| | | |  _| | | |  _ \
| |  | | (_) | (_| | |   \ V / | |  | | | |_| | |_| | |_) |
|_|  |_|\___/ \__,_|_|    \_/  |_|  |_|  \____|____/|____/

 ____  _             _         _                    _          _
|  _ \| |_   _  __ _(_)_ __   | |    ___   __ _  __| | ___  __| |
| |_) | | | | |/ _` | | '_ \  | |   / _ \ / _` |/ _` |/ _ \/ _` |
|  __/| | |_| | (_| | | | | | | |__| (_) | (_| | (_| |  __/ (_| |
|_|   |_|\__,_|\__, |_|_| |_| |_____\___/ \__,_|\__,_|\___|\__,_|
               |___/
        """)
    gdb.set_convenience_variable("moarvm_gdb_plugin_banner_shown", 1)

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

        uint8_t  = gdb.lookup_symbol("MVMuint8")[0].type.strip_typedefs()
        uint8p_t = uint8_t.pointer()

        stooge_t = gdb.lookup_symbol("MVMObjectStooge")[0].type.strip_typedefs()
        stoogep_t = stooge_t.pointer()

        mvmstr_t = gdb.lookup_symbol("MVMString")[0].type
        mvmstrp_t = mvmstr_t.pointer()

    except gdb.error as ex:
        print("Couldn't get types!?", ex)

    init_repr_types_and_structs()

    say_hello()
