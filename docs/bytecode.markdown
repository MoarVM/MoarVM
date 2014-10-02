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
    |    32-bit unsigned integer; since we'll never reach a   |
    |    huge number of versions, this also doubles up as a   |
    |    check that no weird big/little endian issues keep us |
    |    from reading the bytecode.                           |
    +---------------------------------------------------------+
    | Offset (from start of file) of the SC dependencies      |
    | table                                                   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of entries in the SC dependencies table          |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Offset (from start of file) of the extension ops table  |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of entries in the extension ops table            |
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
    | Offset (from start of file) of the annotation segment   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Length of the annotation segment                        |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | HLL Name                                                |
    |    32-bit unsigned integer index into the string heap,  |
    |    providing the name of the HLL this compilation unit  |
    |    was compiled from. May be the empty string.          |
    +---------------------------------------------------------+
    | Main entry point frame index + 1; 0 if no main frame    |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Library load frame index + 1; 0 if no load frame        |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Deserialization frame index + 1; 0 if none              |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

## Strings heap
This segment contains a bunch of string data. Each string is laid out as:

    +---------------------------------------------------------+
    | String length in bytes                                  |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | String data encoded as UTF-8                            |
    |    Bunch of bytes, padded at end to 32 bit boundary     |
    +---------------------------------------------------------+

## SC Dependencies Table
This table describes the SCs that the bytecode in this file references
objects from. The wval opcode specifies an index in this table and and
index in the SC itself. When the bytecode file is first loaded, we look
in the known SCs table and resolve all that we can. Then, the deserialize
code for the compilation unit is run. Whenever the SC creation opcode is
used, we search all known compilation units to see if they have any
unresolved SCs, and fill in any gaps that correspond to the newly created
SC. By the time the deserialize phase for a compilation unit is over, we
expect that all SCs have been resolved. Thus, the lifetime of an SC is
equal to the lifetimes of all the compilation units that reference it,
since their code depends on it. Note that the primary way an SC is rooted
is through a compilation unit, and that these roots are established as
soon as it is created, and before it's returned to userspace (which could
allocate more) are the way we make sure it isn't collected too early.

    +---------------------------------------------------------+
    | Index into the string heap of the SC unique ID          |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

## Extension ops table

    +---------------------------------------------------------+
    | Index into the string heap of the extension op ID       |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Operand descriptor                                      |
    |    Bunch of bytes describing a single operand each,     |
    |    zero-padded to 8 bytes                               |
    +---------------------------------------------------------+

The operand descriptor follows the same format as used by MVMOpInfo.
The 8 bytes limit corresponds to MVM_MAX_OPERANDS.

## Frames Data
The frames data segment contains data that describes all of the frames in
the compilation unit. It also points into the bytecode segment, which contains
the bytecode we will execute for this frame. This is stored elsewhere at least
partly for the sake of demand paging and CPU cache efficiency; once we
processed the static data, it's not very interesting at runtime, so there's no
real reason for it to stay in memory, let alone be cached by the CPU. The
actual bytecode itself, on the other hand, is (at least until JIT happens) of
interest for execution.

Each frame starts with the following data.

    +---------------------------------------------------------+
    | Bytecode segment offset                                 |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Bytecode length in bytes                                |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of locals/registers                              |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of lexicals                                      |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Compilation unit unique ID                              |
    |    32-bit string heap index                             |
    +---------------------------------------------------------+
    | Name                                                    |
    |    32-bit string heap index                             |
    +---------------------------------------------------------+
    | Outer                                                   |
    |    16-bit frame index of the outer frame. For no outer, |
    |    this is set to the current frame index.              |
    +---------------------------------------------------------+
    | Annotation segment offset                               |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of annotations                                   |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Number of handlers                                      |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Frame flag bits                                         |
    |    16-bit integer                                       |
    |    1 = frame has an exit handler                        |
    |    2 = frame is a thunk                                 |
    |    Remaining values reserved                            |
    | [NEW IN VERSION 2]                                      |
    +---------------------------------------------------------+
    | Number of entries in static lexical values table        |
    |    16-bit integer                                       |
    | [NEW IN VERSION 4]                                      |
    +---------------------------------------------------------+
    | Code object SC dependency index + 1; 0 if none          |
    |    32-bit unsigned integer                              |
    | [NEW IN VERSION 4]                                      |
    +---------------------------------------------------------+
    | SC object index; ignored if above is 0                  |
    |    32-bit unsigned integer                              |
    | [NEW IN VERSION 4]                                      |
    +---------------------------------------------------------+

This is followed, for each local, by a number indicating what kind of
local it is. These are stored as 16-bit unsigned integers.

    int8        1
    int16       2
    int32       3
    int64       4
    num32       5
    num64       6
    str         7
    obj         8

Lexicals are similar, apart from each entry is preceded by a 32-bit unsigned
index into the string heap, which gives the name of the lexical.

[Conjectural: a future MoarVM may instead do these in terms of REPRs.]

Next comes the handlers table. Each handler has an entry as follows:

    +---------------------------------------------------------+
    | Start of protected region. Inclusive offset from start  |
    | of the frame's bytecode                                 |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | End of protected region. Exclusive offset from start of |
    | the frame's bytecode                                    |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Handler category mask bitfield                          |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Handler action (see exceptions spec for values)         |
    |    16-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Register number containing the block to invoke, for a   |
    | block handler.                                          |
    |    16-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Handler address to go to, or where to unwind to after   |
    | an invoked handler. Offset from start of the frame's    |
    | bytecode.                                               |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

From version 4 and up, this is followed by a static lexical values
table. Each entry is as follows:

    +---------------------------------------------------------+
    | Lexical index                                           |
    |    16-bit unsigned integer                              |
    +---------------------------------------------------------+
    | Flag                                                    |
    |    16-bit unsigned integer                              |
    |    0 = static lexical value                             |
    |    1 = container var (cloned per frame)                 |
    |    2 = state var (cloned per closure)                   |
    +---------------------------------------------------------+
    | SC dependency index                                     |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+
    | SC object index                                         |
    |    32-bit unsigned integer                              |
    +---------------------------------------------------------+

## Callsites Data
This data blob contains all of the callsite descriptors that are used in
the compilation unit. At the point of loading the bytecode, they will
be set up, and a table pointing to them created. This means that a
callsite descriptor will always be a pointer + offset away.

Each callsite consists of a 16-bit unsigned integer indicating the number
of argument flags. This is followed by the flags, taking 8 bits each. If
the number of argument flags is odd, then an extra padding byte will be
written afterwards. Since version 3, this is then followed with one index
to the string heap (in the form of a 32-bit integer) for each argument flag
that has the `MVM_CALLSITE_ARG_NAMED` bit set.

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
    rl      lexical variable being read, 16 bits unsigned for the
            index within a frame and 16 bits for how many frames out
            to go to locate it
    wl      lexical variable being written, 16 bits unsigned for the
            index within a frame and 16 bits for how many frames out
            to go to locate it
    i16     16-bit integer constant
    i32     32-bit integer constant
    i64     64-bit integer constant
    n32     32-bit floating point constant
    n64     64-bit floating point constant
    si      Strings table index, 32 bits unsigned
    sci     Serialization Context object table index, 16 bits unsigned
    csi     Callsite table index, 16 bits unsigned
    ins     Instruction offset from frame start (for goto), 32 bits unsigned

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

The set of ops is listed in src/core/oplist.

## Annotation segment
This consists of a number of 10-byte records, composed of:

* 32-bit unsigned integer offset into the bytecode segment
* 32-bit unsigned integer strings heap index (filename)
* 32-bit unsigned integer (line number)
