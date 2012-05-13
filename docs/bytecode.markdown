# Bytecode
This document describes the bytecode that the VM interprets or JIT compiles.
Note that this is just one part of an input file to the VM; along with it
will also be a bunch of serialized objects, and some container. This just
describes the way the executable segment of things look. (In a sense, this
is the low-level reification of the Actions/World distinction at the level
of the compiler).

## Endianness
All integer values are stored in little endian format.

## Floats
Floating point numbers are represented according to IEEE 754.

## Header
The header appears at the start of the MoarVM bytecode file, and indicates
what it contains.

    +---------------------------------------------------------+
    | "MOARVM\r\n"                                            |
    |    8-byte magic string; includes \r\n to catch mangling |
    |    of line endings                                      |
    +---------------------------------------------------------+
    | Version                                                 |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the SC dependencies      |
    | table                                                   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of entries in the SC dependencies table          |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the referenced objects   |
    | table                                                   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of entries in the referenced objects table       |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the frames data segment  |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of frames we should end up finding in the frames |
    | data segment                                            |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the callsites data       |
    | segment                                                 |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of callsites we should end up finding in the     |
    | callsites data segment                                  |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the strings heap         |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of entries in the strings heap                   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the SC data segment      |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Length of the SC data segment                           |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the bytecode segment     |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Length of the bytecode segment                          |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

## SC Dependencies Table
This table describes the SCs that the bytecode in this file references
objects from. The index in this table will be used in the referenced
objects table; this table just contains information on how to locate the
SCs.

    +---------------------------------------------------------+
    | Index into the string heap of the SC unique ID          |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

## Referenced Objects Table
This table describes the objects from serialization contexts that are
referenced throughout the bytecode in the compilation unit specified by
this chunk. These will all be dereferenced at the point of loading the
bytecode, so every referenced object will just be a pointer + offset
away. The index in this table is used in the bytecode stream to refer
to the object.

    +---------------------------------------------------------+
    | Index into the SC dependencies table, stating which SC  |
    | the object comes from                                   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Index of the object in the referenced SC                |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

## Frames Data
The frames data segment contains data that describes all of the frames in
the compilation unit. It also points into the bytecode segment, which contains
the bytecode we will execute for this frame. This is stored elsewhere at least
partly for the sake of demand paging and CPU cache efficiency; once we
processed the static data, it's not very interesting at runtime, so there's no
real reason for it to stay in memory, let alone be cached by the CPU. The
actual bytecode itself, on the other hand, is (at least until JIT happens) of
interest for execution.

Each block has the following data.

    +---------------------------------------------------------+
    | Bytecode segment offset                                 |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Bytecode length in bytes                                |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

XXX Much more to do here.

## Callsites Data
This data blob contains all of the callsite descriptors that are used in
the compilation unit. At the point of loading the bytecode, they will
be set up, and a table pointing to them created. This means that a
callsite descriptor will always be a pointer + offset away.

XXX TODO

## Bytecode segment
This consists of a sequence of instructions. Instruction codes are always
16 bits in length. The first 8 bits describe an instruction "bank", and the
following 8 bits identify the instruction within that bank. Instruction banks
0 through 127 are reserved for MoarVM core ops or future needs. Instruction
banks 128 through 255 are mappable per compilation unit, and are used for
"plug-in" ops.

Opcodes may be followed by zero or more operands. The instruction set will
have the needed operands described by the following set of descriptors.

    r       local variable index being read, 16 bits unsigned
    w       local variable index being written, 16 bits unsigned
    rl      lexical variable index being read, 16 bits unsigned
    wl      lexical variable index being written, 16 bits unsigned
    lo      lexical variable outer chain count, 16 bits unsigned
    i16     16-bit integer constant
    i32     32-bit integer constant
    i64     64-bit integer constant
    n32     32-bit floating point constant
    n64     64-bit floating point constant
    si      Strings table index, 16 bits unsigned
    sci     Serialization Context object table index, 16 bits unsigned
    csi     Callsite table index, 16 bits unsigned
    ins     Instruction offset (for goto), 16 bits signed

Note that this ensures we always keep at least 16-bit alignment for ops.

Some instructions place demands on the type of value in the register.
This is perhaps most noticable when it comes to integers of different
sizes; all computations are done on them in full-width (64-bit) form,
and loading/storing them to registers representing locals of more
constrained sizes needs explict sign extension and truncate ops.
Wherever a register is specified, the kind of value in it is also
indicated. These are typechecked *once*, either at bytecode load time
or (perhaps better) on the first execution. After that, all future
interpreter executions can just plow through the instructions without
ever having to do checks.

Bank 0 is for control flow and primitive operations.

    0x00    no_op                               do nothing
    0x01    goto        ins                     unconditional branch
    0x02    if_i        r(int64), ins           branch if non-zero
    0x03    unless_i    r(int64), ins           branch if zero
    0x04    if_n        r(num64), ins           branch if non-zero
    0x05    unless_n    r(num64), ins           branch if zero
    0x06    if_s        r(str), ins             branch if string not empty
    0x07    unless_s    r(str), ins             branch if string empty
    0x08    if_s0       r(str), ins             branch if string not empty and not '0'
    0x09    unless_s0   r(str), ins             branch if string empty or '0'
    0x0A    if_o        r(obj), ins             branch if true (boolification protocol)
    0x0B    unless_o    r(obj), ins             branch if false (boolification protocol)
    0x0C    set         r(`1), r(`1)            copy one register value to another
    0x0D    extend_u8   w(int64), r(int8)       unsigned integer extension (8 to 64)
    0x0E    extend_u16  w(int64), r(int16)      unsigned integer extension (16 to 64)
    0x0F    extend_u32  w(int64), r(int32)      unsigned integer extension (8 to 32)
    0x10    extend_i8   w(int64), r(int8)       signed integer extension (8 to 64)
    0x11    extend_i16  w(int64), r(int16)      signed integer extension (16 to 64)
    0x12    extend_i32  w(int64), r(int32)      signed integer extension (32 to 64)
    0x13    trunc_u8    w(int8), r(int64)       unsigned integer truncation (8 to 64)
    0x14    trunc_u16   w(int16), r(int64)      unsigned integer truncation (16 to 64)
    0x15    trunc_u32   w(int32), r(int64)      unsigned integer truncation (8 to 32)
    0x16    trunc_i8    w(int8), r(int64)       signed integer truncation (8 to 64)
    0x17    trunc_i16   w(int16), r(int64)      signed integer truncation (16 to 64)
    0x18    trunc_i32   w(int32), r(int64)      signed integer truncation (32 to 64)
    0x19    extend_n32  w(num64), r(num32)      float extension
    0x1A    trunc_n32   w(num32), r(num64)      float truncation
    0x1B    get_lex     w(`1), rl(`l)           get lexical from this frame
    0x1C    bind_lex    wl(`1), r(`l)           bind lexical from this frame
    0x1D    get_lex_lo  w(`1), lo, rl(`l)       get lexical from outer frame
    0x1E    bind_lex_lo wl(`1), lo, r(`l)       bind lexical from outer frame
    0x1F    get_lex_ni  w(int64), str           get integer lexical by name
    0x20    get_lex_nn  w(num64), str           get number lexical by name
    0x21    get_lex_ns  w(str), str             get string lexical by name
    0x22    get_lex_no  w(obj), str             get object lexical by name
    0x23    bind_lex_ni str, r(int64)           bind integer lexical by name
    0x24    bind_lex_nn str, r(num64)           bind number lexical by name
    0x25    bind_lex_ns str, r(str)             bind string lexical by name
    0x26    bind_lex_no str, r(obj)             bind object lexical by name
    0x27    return_i    r(int64)                return an integer
    0x28    return_n    r(num64)                return a number
    0x29    return_s    r(str)                  return a string
    0x2A    return_o    r(obj)                  return an object
    0x2B    return                              return (presumably to void context)

Bank 1 is for 6model related operations.
