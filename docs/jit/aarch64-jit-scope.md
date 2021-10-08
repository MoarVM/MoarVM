# MoarVM JIT AArch64 Port Work Scope


## Introduction

This document is a **DRAFT** attempt to scope the work necessary to do a full
working port of the MoarVM JIT to AArch64/ARM64 (64-bit ARM architecture).

AArch64 was announced in 2011 with ARMv8-A and has been used in
publicly-available devices since at least 2014.  At this point (late 2021),
AArch64 is used in the full range of consumer ARM-based devices from the
Raspberry Pi 3 to the Nintendo Switch to the Apple M1 and A-series, even
scaling up to the fastest public supercomputer in the world today; most non-x64
CPUs currently sold are AArch64.

MoarVM currently supports JIT compilation only on x64/x86-64; on other
architectures it is limited to (runtime-specialized) bytecode interpretation.
The popularity of AArch64 -- especially in power-constrained systems -- makes
it a good option to be the second JIT port.

Unfortunately, while the first JIT architecture is no doubt the hardest (all of
the infrastructure had to be built from scratch), the second architecture is
likely to expose all of the hidden assumptions in the initial implementation,
and thus likely to involve much extra work to clean them up.  This document
takes a look at what that might entail, in order to scope the AAarch64 port.


## Known JIT Porting Risks

A few porting risks are already known, and detailed below.  Note that there are
likely more that have not been discovered, and will need to be scoped as they
appear.


### DynASM Limitations

AArch64 has been added to DynASM, but there may not be support for dynamic
register assignment, as required by MoarVM; this feature was developed for x64
because MoarVM needed it.  If not already supported, this work will have to be
ported and pushed upstream.


### Register Allocation

The MoarVM JIT register allocator is relatively complex.  While it was designed
to be ABI-agnostic, this hasn't been tested and the RA author believes it
likely that additional work will be needed to support the various AArch64 ABIs
(at least MacOS and Linux).


### Little-Endianness

x64 is a little-endian-only architecture, and thus the MoarVM JIT has never had
to work with big-endian platforms.  AArch64 is bi-endian (able to run in both
little- and big-endian modes), though defaults to little-endian so initial
porting work can probably begin without addressing this.  Still, a full JIT
port will need to handle big-endian operation, and even the MoarVM interpreter
has required a few big-endian fixes this year.


### Alignment Strictness

AArch64, like many RISC architectures, has much tighter data and instruction
alignment requirements than x64; some instructions can't even *refer* to
unaligned memory positions, let alone handle them correctly.  Some alignment
requirements are wider than a machine word, so "natural alignment" is
insufficient; the SP (Stack Pointer) must always be aligned on a 16-byte
boundary for instance.  Because x64 (and x86 before it) have been the primary
MoarVM development platform so far and are very alignment-forgiving, it is very
likely that alignment issues exist in the MoarVM codebase, though as with
endianness, alignment issues in the interpreter do get addressed over time.

Note that while some alignment issues might trap (potentially crashing MoarVM),
there are also a fair number of alignment issues that will only show up in
reduced performance, such as memory accesses that cross cache lines.  While
trap crashes will at least be obvious (though possibly difficult to diagnose),
alignment issues that only drain performance will be much more difficult to
notice in the first place.


### Incomplete Unsigned Handling

Both the MoarVM interpreter and x64 JIT treat unsigned native ints as signed on
certain paths.  This already causes problems in some cases (on all
architectures) with uints that have their msb set, and as the AArch64 ISA is
dependent for many basic operations on correct sign handling, these sign
inconsistencies must be fixed as a prerequisite.


### Memory Access Instructions

While x64 has many address modes that allow arithmetic and logic work to be
combined with memory access, AArch64 has very few of these.  (Yes, it's a
load/store architecture, but it also has pre- and post-update index modes.)
While it is clear that MoarVM and its JIT were architected with
register-register architectures in mind, the conversion from abstract to
actual machine architecture will need to be audited for memory access
assumptions.


### Different Operation Costs

Certain operations that are considered free (or at least inexpensive) on x64
are not on AArch64 (and vice-versa).  For example, integer remainder is a
"free" side effect of division on x64, but there is no direct remainder or
modulus operation on AArch64.  Integer division produces only the quotient, and
modulus must then be calculated from the quotient using an extra multiply and
subtraction.  On the other hand, most arithmetic operations on AArch64 can
freely choose whether or not to affect the condition code flags, avoiding
instruction juggling just to avoid corrupting the CC flags before a conditional
branch.  Likewise, AArch64 stack handling is optimized for handling pairs of
registers at a time, a spilling complication that x64 does not need to deal
with, and a few fused operations are provided such as multiply-add and
compare-branch.

Aside from ISA differences, there are also *micro*architectural differences,
most notably which instructions can be coscheduled and which will cause stalls
or pipeline bubbles.  Of course with a great many processors available for both
x64 and AArch64, there are bound to be many differences within each camp, but
there are some fundamental distinctions in base expectations as well, such as
whether integer shifter units are precious (many x64) or fully populated.

Thus it is likely that the base "tiles" used in the x64 JIT are not the best
match to produce efficient machine code on AArch64.  Rather than simply port
those tiles, it will be necessary to review what makes sense in the context of
all jitted MoarVM ops and make broader changes to the JIT, macro library,
etc. to allow completely different base tile sets.


## Required Knowledge

In order to begin, the porter will need to build up (or at least "swap in")
some specialist knowledge.  To be clear, it is NOT necessary for the porter to
*already* be an expert at the start.  Rather, non-trivial time must be
allocated in the work schedule for getting comfortable with these, or
refreshing oneself after a break.  Porters can start shallow and dig deeper as
the need arises.


### MoarVM Abstract Machine

Porters should probably start by understanding how MoarVM fits into the
nqp/Rakudo stack, including the abstract register machine and op portfolio it
offers.  This includes understanding how high level concepts such as lexical
and dynamic variables, control flow and loops, routine invocations, exceptions,
etc. are compiled into MoarVM bytecode.


### Runtime (De-)Optimization

MoarVM performs a number of runtime optimizations before engaging the JIT, such
as type specialization, escape analysis, and call inlining.  This can cause the
actual jitted code to differ greatly from the original bytecode.  Since many of
these optimizations are speculative, it is also necessary to support deopt
(undoing optimizations), and the JIT must support this as well, including
behaving correctly when throwing exceptions through a stack of mixed optimized
and non-optimized routines.


### Concurrency

In order to be concurrency- and thread-safe, MoarVM has primitives and
standards for thread-safe data handling that will need to be respected by JIT
operations, especially those surrounding memory management, memory fencing, and
atomic operations.  Understanding *why* MoarVM's concurrency works the way it
does is also important, because it is nigh-inevitable that differing
consistency rules will expose code depending on x64's forgiving memory model.


### Assembly Language Development

Porters should understand the basics of assembly language development for their
chosen development platform(s): reading ARM assembly, assembling using standard
tools, assembling using DynASM, and debugging of mixed C and assembly code.


### AArch64 and x64

For obvious reasons, porters will need a basic understanding of the AArch64
architecture before getting started, and will need to understand at least
enough of the x64 architecture to identify differences and places in the existing
code that implicitly rely on the x64 architecture's quirks.


### C Calling Conventions

A great many MoarVM ops are thin wrappers around calls into C code, not to
mention MoarVM's support for dynamic C and C++ binding (exposed at the Raku
layer as NativeCall).  Porters will need to understand the differences between
x64 and AArch64 calling conventions in order to convert both the op and
NativeCall C invocations, and understand how the interpreter, specializer, lego
JIT, and expression JIT work together to handle these calls.


## Required Tools/Environments

Aside from the basic knowledge, porters will need a work environment set up for
comfortable AArch64 development.


### Native Development

If you have a sufficiently performant AArch64 (ARMv8-A or greater) system, with
enough spare RAM to compile and debug on, you can directly develop on the ARM
device.  Make sure that you have installed a full 64-bit OS, as some (such as
Raspbian) only work with 32-bit user applications. If you go with this option,
you can use all the usual native build tools; everything Just Works, and Rakudo
builds well on AArch64 at least on Linux and MacOS.

Given sufficient swap space a Raspberry Pi 3 reportedly can be used for this --
it's the minimum RPi with AArch64 support -- but it's known to be *very* slow
for large compiles.  You'll likely have a much better experience using a
Raspberry Pi 4 with at least 4GB RAM, or some other more powerful system.  Make
sure to use sufficient heat management (heat sinks, fans, a heat-distributing
metal case, etc.).  Long multi-core compile and test runs can easily overheat a
Raspberry Pi in its stock fanless plastic case, with a host of annoying
symptoms.

For a much higher-performance development environment, you can use an Apple M1.
Rakudo is known to build cleanly there (without JIT, of course), but do note
that the Apple AArch64 C ABI is different from the POSIX/Linux AArch64 C ABI.
Ideally we'd like to support both of these in the AArch64 JIT.

If you'd rather rent than buy, several cloud providers offer AArch64 VMs
(either running on native ARM hardware or emulated), and several offer high-end
options if you want lots of fast cores and RAM.

Finally, on a more limited basis, some FOSS groups such as the GCC team allow
other FOSS projects to apply for access to build farms with many different
architectures and variants represented.


### QEMU and Cross-Compiles

MoarVM supports cross-compilation, so you can install cross-compile tools on
your usual development platform to produce AArch64 binaries.  You can then copy
these to ARM hardware for testing, or use QEMU (or a similar processor
emulator) to run the binaries locally under emulation.

Note that MoarVM cross-compilation is not regularly tested, so may have some
bitrot since it was last used; depending on how much has changed, this could
take some effort to bring up to date.

Also note that some emulators do not fully emulate all failure modes.  For
example, QEMU may silently execute unaligned accesses that would trap on real
hardware.


## Roadmap Sketch

The following roadmap sketch gives a rough idea of one possible plan of attack.
It is by no means the only reasonable approach, which will vary with previous
experience and available tools.


### 1. Set Up a Development Environment

Choose one of the paths above to setting up build and test environments, and
ensure ability to check out, build, and test a full AArch64 stack (MoarVM, nqp,
and Rakudo).  Don't skip this!  It will only bring heartbreak/painful debugging
later.


### 2. Study the Basics

It's not necessary to study everything in the Required Knowledge section deeply
at this point, just to get some of the mental models in place.  At this stage a
shallow tour through the following should suffice:

* The MoarVM abstract machine and the MoarVM/nqp/Rakudo stack
* AArch64: Basic architecture, ISA, and assembly language
* AArch64: C ABI (calling conventions)
* Mixed C/assembly language development with DynASM
* MoarVM interpreter/lego JIT/expression JIT interaction
* MoarVM debugging tools


### 3. Initial Proof of Concept

Design, implement, build, and test the most basic bits **in a branch or fork**:

* Decide on register usage: thread globals, temporaries, reserved registers
* Add standard function prolog and epilog implementations
* Lego JIT basic ops
* Lego JIT MoarVM C function calls
* Enable and test AArch64 lego JIT


### 4. Expression JIT

With a proof of concept under the lego JIT working, start implementing
expression JIT support **in a branch or fork**:

* Implement and test basic tiles for AArch64
* Determine impedance mismatch between current basic tiles and AArch64
* Expand and/or refactor tileset for more efficient AArch64 support
  (without breaking efficient x64 support)


### 5. Polish and Verify

This is the time to make sure the AArch64 branch is really solid and safe:

* Validate that JIT attempts mostly succeed, with few bails or errors
* Test with nqp build/tests, Rakudo build/tests, and Raku spectest/stresstest
* Run Blin (ecosystem tests) against the branch, on AArch64, x64, **and**
  non-jitted platforms, to ensure that AArch64 changes did not break existing
  platform support
* Rebase on current `main` branch; fix conflicts and clean commit history if
  needed


### 6. Tune and PR

Final steps before the big merge:

* Benchmark without JIT, with lego JIT only, and with all JIT enabled
* Investigate and fix any performance regressions or anomalies
* Rebase onto `main` branch again
* Final round of tests (see step 5 for details)
* Create GitHub Pull Request (this should automatically notify the MoarVM team)


## Alternate Paths

There are a couple alternate development paths that *might* be advantageous,
but it will be difficult to tell whether either would be a net win until at
least steps 1 & 2 (and perhaps 3) have already been completed:


### Refactor JIT Infrastructure First

Currently the expression JIT depends on the lego JIT, because the latter
provides much of the infrastructure needed by any MoarVM JIT.  Before beginning
the actual AArch64 porting, this path would first disentangle the code
generation parts of the lego JIT from the general JIT infrastructure, and
rebase the expression JIT directly on the infrastructure parts.  This allows
the lego JIT to be either dropped completely, or used more aggressively as a
first-pass low-optimization JIT before code becomes hot enough to involve the
optimizing JIT.

If dropping the lego JIT completely, note that there are some optimizations
such as reprop devirtualization that only exist in the lego JIT; these would
need to be ported to the expression JIT.  At the same time, it would be useful
to reduce the optimization and code generation time of the expression JIT, as
its extra runtime cost can end up a net loss (especially considering that
currently when the the specialization/JIT thread is running, runtime statistics
aren't being gathered from the other threads, possibly leading to incorrect
optimization decisions).


### Replace the Existing JIT With a Public Project

Some existing FOSS projects are attempting to be general JIT engines, providing
a common API to many compiler front-ends and promising optimized code
generation for many different platforms.  Some of these (such as the GCC and
Clang code generators) are probably too heavyweight to work well for MoarVM's
use case, having very large memory footprints, slow startup, and/or hundreds of
optimization passes that individually provide only marginal performance gains.

There are some projects such as
[MIR](https://developers.redhat.com/blog/2020/01/20/mir-a-lightweight-jit-compiler-project)
that aim for smaller footprint with fast optimizers, but we then run into a
larger problem.  Each JIT wants to use its own intermediate representation, but
MoarVM bytecode already *is* an intermediate representation, based on an
opinionated abstraction oriented towards Raku's specific needs, possibly
resulting in significant impedance mismatches.  For example, it's unlikely that
most third-party JIT engines have native support for large integers, or the
auto-upgrading smallint optimization.  Trying to bridge those IR impedance
mismatches in order to use a third-party JIT may be significantly more effort
than improving the existing JIT based on lessons learned from those other
projects.


## Examples


### Tileset Differences

On x64 for the unsigned integer case at least, both division and modulus can
be expressed in terms of the same underlying machine operation, `divmod`, with
equal cost:

```
(div (divmod $n $d)) -- div, take `rax`
(mod (divmod $n $d)) -- mod, take `rcx`
```

On AArch64, division is a simple operation, but modulus is a compound operation
with higher cost.  In existing tile syntax, this looks very expensive, using 3
underlying operations rather than 1:

```
(mod (sub ($n
          (mul $d
               (div $n $d)))))
```

However, on AArch64 the first and second lines can be fused into a single
operation, `smul` (which is approximately `$a = $b - $c * $d`):

```
(smul (sub $a (mul $b $c)))
(mod (smul $n $d
              (div $n $d)))
```

In other words, to recover some of the lost modulus performance on AArch64, the
tileset must include a primitive operation that has no x64 equivalent.


## Resources

The following resource links, discovered while researching this document, are
alphabetized by subject category:


### AArch64

* [ARMv8-A official reference manual](https://developer.arm.com/documentation/ddi0487/gb/)
* [AArch64 Wikipedia overview](https://en.wikipedia.org/wiki/AArch64)
* [AArch64 introductory lectures](https://www.cs.princeton.edu/courses/archive/spr19/cos217/lectures/) -- university courseware; AArch64 starts with lecture 13
* [AArch64 assembly guide](https://modexp.wordpress.com/2018/10/30/arm64-assembly/) -- from the viewpoint of ARM pentesting (security penetration testing)
* [AArch64 on Rosetta Code](https://rosettacode.org/wiki/Category:AArch64_Assembly)
* AArch64 alignment penalty Stack Overflow questions:
  * [Alignment question 1](https://stackoverflow.com/questions/38535738/does-aarch64-support-unaligned-access)
  * [Alignment question 2](https://stackoverflow.com/questions/45714535/performance-of-unaligned-simd-load-store-on-aarch64)


### Apple M1 Microarchitecture

* [Apple M1 Firestorm](https://dougallj.github.io/applecpu/firestorm.html)
* [Apple M1 Icestorm](https://dougallj.github.io/applecpu/icestorm.html)


### Build Farms

* CFarm (GCC Compile Farm Project)
  * [CFarm machine list](https://cfarm.tetaneutral.net/machines/list/)
  * [CFarm account request](https://cfarm.tetaneutral.net/users/new/)


### DynASM (Dynamic Assembler)

* [DynASM home page](https://luajit.org/dynasm.html)
* [DynASM source](https://github.com/LuaJIT/LuaJIT/tree/v2.1/dynasm)


### MIR JIT

* [MIR overview](https://developers.redhat.com/blog/2020/01/20/mir-a-lightweight-jit-compiler-project)
* [MIR public repo](https://github.com/vnmakarov/mir)


### MoarVM JIT

* [MoarVM JIT documentation](https://github.com/MoarVM/MoarVM/tree/master/docs/jit)
* [MoarVM JIT source](https://github.com/MoarVM/MoarVM/tree/master/src/jit)
* [MoarVM JIT x64 specifics](https://github.com/MoarVM/MoarVM/tree/master/src/jit/x64)


### Performance Limits

* [Recognizing microarchitectural speed limits](https://travisdowns.github.io/blog/2019/06/11/speed-limits.html)


### x64

* [Intel official optimization reference manual](https://software.intel.com/content/www/us/en/develop/download/intel-64-and-ia-32-architectures-optimization-reference-manual.html)
* [Agner's x86/x64 optimization resources](https://www.agner.org/optimize/)
