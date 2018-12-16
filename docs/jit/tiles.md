# Tiles

This document describes the role of tiles and how to implement
additional tiles for an architecture.

## What is a tile?

A tile is the combination of a *tree pattern* and a *code generation
rule*. The patterns are defined in a tile list file. Tiles are
architecture-specific, unlike expression templates. The path for the
x64 tile list is 'src/jit/x64/tiles.list'. If and when someone chooses
to port the JIT compiler, other architectures will need to define
their own tile list files.

The textual format used for tiles is, as in the expression templates,
symbolic expressions (s-expressions). They are described in some
detail in that document (expr.md). A pattern is matched and replaced
with the *nonterminal*, which are then used in other pattern matches.
For instance, given the following tree:

    (add
        (load
            (addr
                (local)
                8
            )
            int_sz
        )
        (const
            16
            int_sz
        )
    )

Which, by the way, stands for:

    = LOCAL[8] + 16

And the following simplified set of patterns:

    (add reg reg)  -> reg
    (local)        -> reg
    (load reg sz)  -> reg
    (addr reg ofs) -> reg

We can do the following bottom-up transformation:

<pre><code>
(add (load (addr <b>(local)</b> 8) int_sz) (const 16 int_sz))
(add (load <b>(addr <i>reg</i> 8)</b> int_sz) (const 16 int_sz))
(add <b>(load <i>reg</i> int_sz)</b> (const 16 int_sz))
(add <i>reg</i> <b>(const 16 int_sz)</b>)
<b>(add reg <i>reg</i>)</b>
<i>reg</i>
</code></pre>

In this fashion tile patterns are used to succesively simplify the
input tree to a final terminal symbol. The actual process to implement
this is complicated and beside the point of this article. The reason
to care about it is that when using a complex instruction set (CISC)
architecture any instruction can typically implement a large part of
any expression tree, and doing so is often worth it.


## How to Write a Tile

As I'm writing this documentation, the tile library is not yet
complete, nor will it be for the foreseeable future. This is primarily
because the instruction set provided by x64 processors is large; there
are many possible sets of instructions for any given computation. So
my aim is to help interested parties in completing the tile library,
which I hope should be a rewarding passtime. I warn you that a little
(but not a lot) assembly language knowledge is required.

A tile requires a pattern description in the tile list file. Say I'm
writing a tile for the hardware implementation of the binary XOR
operator. In x64 we have a number of options, including but not
limited too:

    xor rax, rcx         /* register-to-register */
    xor rax, 0x0ba1      /* constant to register */
    xor rax, [rcx+ofs]   /* memory with offset to register */
    xor rax, [rcx+rdx*8] /* indexed memory to register */
    xor [rax+ofs], rcx   /* register to memory */
    xor [rax+ofs], 0x12  /* constant to memory */

Let's focus on the first three, since it is my suspicion that these
will be the most common (the latter two imply a STORE and a LOAD from
the same operand, which is not directly supported in the memory model
of the compiler, although it may be in the future).

A tile description is a list that starts with the keyword
`tile:`, followed by the tile *name*, the pattern proper,
the *symbol* that it generates, and the (estimated) tile *cost*. There
are three supported symbols, namely __reg__, __flag__ and
__void__. The __flag__ symbol refers to the output of a comparison or
'testing' operation and is in general only useful in conditional
operations such as IF and WHEN. The __reg__ symbol refers to a
physical register and is what we'll be using for the xor
implementations. (This is again not future-exclusive, because I may
want to add different symbols for numeric and/or SIMD registers). The
__void__ symbol represents operations which yield no results. (NB: in
the past there was a __mem__ symbol that stood for (compound) memory
access. This approach has been abandoned after it was clear

The first version of `xor` corresponds to the following
pattern:

    (tile: xor (xor reg reg) reg 2)

Meaning it is a tile called 'xor', matching the expression tree node
`XOR` (or `MVM_JIT_XOR`) and two register
arguments, yields a register, and has a cost of 2. The derrivation of
costs is explained below. By constrast, the second and third version
correspond to these patterns:

    (tile: xor_consst (xor reg (const)) reg 3)
    (tile: xor_addr   (xor reg (load (addr reg))) reg 6)

You may notice that the `(const)` and `(addr
reg)` nodes are stripped from their argument values. That is
because argument values do not take part in tile matching. As a
consequence, tiles should be written to accept (or reject) any
arguments they will encounter.

By the way, it is by no means necessary that a tile represents a
single machine code instruction, although this is the case for most
tiles currently implemented.

Finally, I note that the symbol names you choose in your tile must
have an analogue defined somewhere in a suitable C header file, like
`MVM_JIT_REG` and `MVM_JIT_FLAG` which are
defined in src/jit/expr.h; and also that all values marked
`reg` are managed by the register allocator (and only those
values). I'm still looking for a good way to represent constraints on
registers, so expect some changes in the future. More details on the
interaction of tiles with the register allocator are described below.


## Calculating costs

Calculating the proper cost of a tile is something of a dark art form
because there are many different factors to take into account. Some of
the more important ones are:

* Code size - the machine code cache is pretty small I've heard, so
  smaller code is generally better for performance
* Memory access - memory access is often unavoidable from the point of
  view of the tiler, but memory reads and writes are expensive and
  this cost should be reflected in the tile.
* Register use - registers are a limited resource and spilling them
  involves memory traffic, so the (temporary) use of a register should
  receive a cost penalty.

With that in mind, I use the following scheme to calculate costs:

* Per instruction issued, I count 1. Per constant stored in the
  instruction stream, I also count 1. Hence `(xor reg
  (const))` is more expensive than `(xor reg reg)`.

* Per register used as output or as temporary variable space, I
  count 1. This is irrelevant for 'call' nodes which spill
  values. Note that using *input* registers is free, since their costs
  are calculated elsewhere. Hence `(xor reg (const))`
  (total cost 3) is cheaper than `(const) + (xor reg reg)`
  (total cost 4), because the second version uses 2 instructions and
  two registers. 

  In contrast, operations yielding `flag`
  values don't pay for registers - the cost of converting flags
  to register values if necessary is paid by other operations.

* Per memory access operation, I count a cost of 4. To be fair, this
  isn't completely relevant since memory-access operations do not
  usually compete with non-memory-access operations (the memory
  traffic is either explicitly required by the code or implicitly by
  the register allocator, and not very often optional). Still, by
  using the same cost values consistently the tiler can generate
  correct tables.

## Interaction with the register allocator

The basic memory model of the new JIT is register to register
operations. (By the way at, this is at odds with the rest of the VM,
which uses memory-to-memory operations, which is unavoidable if you're
developing a *virtual* machine.). Operations are generally expected to
write values to registers and to read values, ultimately from
registers. Thus, the expression tree format that we are tiling is
deliberately closer to the CPU architecture.

The important bit about tiles and registers is that as long as you do
not need any temporary registers or any specific registers, you don't
need to bother with the register allocator at all. Registers will
be automatically allocated to write values to and your register
operands will be equally automatically loaded if ever
spilled. Temporary register allocations cannot cause any 'in-use'
register to be unloaded, nor can temporary registers themselves be
accidentally freed. Also, *do not try to free registers yourself*,
the register allocator will handle that quite adequately and a double
free can wreak havoc in the same way it can do in malloc. There is no
reason to ever do so since the register allocator will free registers
as soon as it is possible to do so.


## Implementing tile code

The tile implementation is responsible for the output of suitable
machine code for it's architecture. For machine code output we use the
[DynASM](http://corsix.github.io/dynasm-doc/tutorial.html)
library. Please read that link for detailed information on DynASM.
With DynASM, tile implementation becomes simple.

The tile implementation is defined in `src/jit/x64/tiles.dasc`. This
file is included from `src/jit/x64/emit.dasc`, which is the file that
includes the original JIT architecture-specific code. A tile
implementation looks much like this:

    /* in src/jit/x64/tiles.dasc, supposing we only care about 8 byte xor */
    MVM_JIT_TILE_DECL(xor) {
        int dst   = tile->values[0];
        int src_a = tile->values[1];
        int src_b = tile->values[2];
        if (values[0]->size < 8)
            MVM_oops(tc, "oops!");
        if (src_a != dst) {
            | mov Rq(dst), Rq(src_a);
        }
        | xor Rq(dst), Rq(src_b);
    }

The lines starting with a '|' are machine code that is to be emitted
to the compiler. Of note is that in the expression model a node takes
any number of input nodes (typically zero, one or two) and yields an
output node (typically a value into a register). Hence the destination
and source nodes are different. However, in x64 (and indeed all modes
of x86) only two operands per instruction are supported, one of which
typically acts as destination operand (as well as a source
operand). Hence the xor operation here described overwrites the first
operand. In case the first operand wil be used later, this is of
course unacceptable. Hence the mov on the 9th line. However, in case
the first operand *isn't* used later, reusing the register can serve
as a minor but significant optimization. The register allocator tries
to ensure that this is the case for all relevant tiles, and tile
writers should try to take advantage of this if possible. Note that
architectures which use more standard (and sane) three-operand
instructions do not have to take this into account. Note also that the
size of the operands is a parameter; usually you should use the
biggest of the child operand sizes as definitive. Please, please,
please check and fail on sizes you do not expect to handle, because
bugs in emitted code are much more difficult to debug than bugs in the
code generator.

It is probably of interest what the exact argument list definition of
a tile really is. A tile function receives the following arguments in
order:

* `MVMThreadContext *tc` - the current thread context
  passed to all MVM functions

* `MVMJitCompiler *compiler` - the compiler object, which
  is aliased via macro magically to a DynASM handle object, which
  magically ensures that the dynasm statements work. Also the general
  manager of labels and registers etc.

* `MVMJitTile *tile` - the tile object that holds the values and
  parameter arguments.

* `MVMJitExprTree *tree` - the tree object that is being
  compiled. Contains pretty much all data relevant to the compilation
  that is not kept in the compiler structure; however in tiles it's
  mostly used to extract argument data, since other data is made
  available in more convenient ways.









