### Extensions and Extension Ops [proposed/tentative]

#### The MoarVM Opcodes Overview

The MoarVM interpreter uses 16-bit opcodes, where the first byte is called the
"bank".  There are currently around 470 built-in ops, and I'd guess there'll be
around 500 once Rakudo's bootstrapped and passing spectest.

The interpreter loop currently dispatches a top level of switch/case by bank,
after reading the next byte in the stream, and then dispatches by switch/case
to the particular op in the bank by reading the next byte in the stream.  It is
not known whether the various C compilers compile these any more efficiently
than a flat switch/case of all 65536 potential opcodes with all cases
enumerated (in order).  However, I suggest that it would be more efficient to
read both bytes into an unsigned short and then do bit  operations to split out
the two bytes... or simply dispatch into one large switch/case.  It's at least
worth testing whether it's faster on some platforms and compilers.

Many opcodes are self-contained (they don't call other C functions, and some
don't even make system calls), but lots and lots of them do call functions.
Since the ./moarvm binary is statically-linked (mostly), the link-time code
generation and optimization by modern compilers should do a pretty good job of
making such things optimal [please excuse the truism].  However, in the case of
dynamically loaded extensions to the VM that need to dynamically load native
libraries with a C ABI (nearly all native libraries have a build that exposes
such a thing), the function pointers must be resolved at runtime after the
library is loaded.  Perl 6's NativeCall module (using dyncall on parrot) can
load libraries by name and enumerate/locate entry points and functions by name.

I propose to use the dyncall functionality to load MoarVM extensions and
resolve function pointers.  The following is a draft design/spec doc for how
that might look:

----------------------------------------------------------------

#### Representing Extension Op Calls on Disk and in Memory

In a table in the .moarvm disk representation of the bytecode, each extension
op invoked from that compilation unit has an entry with: 1. a (16-bit) index
into the string heap representing the fully-qualified (namespace included) name
of the op, and 2. a (16-bit) index into the string heap representing the
signature of the op at the time it was loaded.  In the in-memory (deserialized)
representation of the compilation unit, each record also has room to store the
cache of the function pointer representing the C function to which the op was
resolved.  Each distinct op called from that compilation unit is encoded in the
executable bytecode as its index in the extension op table plus 4096.  The
first 16 banks (of 256 ops each, so 4096) are reserved for the VM's native ops,
and the highest 32768 (highest bit set) are reserved for JIT hooks, so you have
28672 available for unique extension ops called from a single compunit.  The
table on-disk and in-memory extop tables are dense; there are no gaps.

#### Loading Code That Calls Extension Ops

During bytecode validation (on-demand upon first invocation of a frame), when
the validator comes across an opcode >= 8192, it subtracts 8192 and checks that
the resulting index is less than the number of extension op calls proscribed by
the compunit.  Then it gets that extop call record (MVMExtOpCall) from the
table, and if the record has a function pointer, it calls it with the sole arg
(MVMThreadContext tc). If the function call slot is NULL, it means the function
pointer hasn't been resolved for this compunit, but also that the signature
hasn't yet been validated against the version of that opcode that was loaded by
its dependencies (if it was!).  First the validator does a hash lookup to check
whether the extop has been loaded at all (this requires a mutex protection,
unless we're by then using a lock-free HLL hash for this), then if it hasn't,
it throws a bytecode validation exception.  If it has been loaded (by itself or
by a dependency), it compares the signatures of the call in the compunit whose
frame is being validated against the signature of the loaded op by that name,
and if they don't match, throw a bytecode validation exception: "extension op
<opname> call signature mismatch - the op's old signature (xxxx) was
deprecated? You tried to load a call with signature <xxxx>."  If the signatures
matched, operand type validation of the actual passed parameters (register
indexes) proceeds normally, using the extop's signature.  The validator copies
the function pointer from the process-wide registry into the in-memory record
of the extop call in that compunit.

#### Loading Extensions

When a compilation unit is loaded, its "load" entry point routine calls its
INIT block (if it has one), which does something like the example below,
registering the native function pointers and their signatures with the runtime.
It communicates with the runtime via the currently-compiling compiler (as there
is generally a World-aware HLL compiler calling ModuleLoader).  To start, it
simply uses NativeCall to fetch each function pointer (but there are plenty of
optimization opportunities there).

#### Examples

The below example purports to show the contents of a skeleton extension as
it would look to a developer.  (please excuse incorrect syntax; it's pseudo-
code in some places. ;)

helper package (part of the MoarVM/NQP runtime) - MoarVM/CustomOps.p6:

```Perl
package MoarVM::CustomOps;
use NativeCall;

# If we're compiling the innermost layer (and not just loading it at INIT time)
# at *compile-time* of the compilation unit surrounding the INIT block we
# assume we are in, inject the symbol into the innermost World outside of us.
# ALSO, do the same thing at INIT-time (using nqp::extop_install) when we have
# already been compiled, as well as when we're compiling.

sub install_ops($library_file, $c_prefix, $op_prefix, $names_sigs) is export {
    my $world = nqp::hllcompilerworld;
    my $opslib = native_lib($library_file);
    -> $name, $sig {
        my $fqon = "$op_prefix::$name";
        nqp::extop_install($opslib, $fqon, "$c_prefix$name", $sig);
        $world.extop_compile($fqon, $sig)
            if $world.is_compiling;
    } for $names_sigs;
}
```

Above, the nqp::hllcompilerworld op simply retrieves an appropriately named
dynamic lexical of the current in-flight World object.  That class will have an
HLL method named extop_compile, detailed below.

Notice the helper package uses NativeCall to find the function pointers via the
native_function (or whatever it's named) routine.  When each function pointer
is passed to the extop_compile method of the in-flight World object, that
method in the HLL compiler will pass the function pointer to a special internal
opcode (nqp::extop_install) that takes the NativeCall library object, the fully
qualified name of the op as it will appear in the HLL source code (namespace
::opname), and a string representing the register signature (a la parrot's op
signatures), so the bytecode validator knows how to validate its register args.

```Perl
class World { # NQP snippet

# at *compile-time* of the compilation unit surrounding the INIT block
# we assume we are in, inject the symbol into the innermost World outside of
# us.

method extop_compile($fqon, $addr, $sig) {
    my $cu := self.current_compunit;
    my %extops := $cu.extop_calls;
    nqp::die("op $fqon already installed!")
        if nqp::has_key(%extops, $fqon);
    nqp::push($cu.extop_table, $fqon);
    %extops{$fqon} := $cu.next_extop++;
}
```

Since the custom ops are resolved "by name" (sort of) upon bytecode loading,
we don't have to worry about Rakudo bootstrapping, since in order to install
the custom ops for Rakudo, we can simply rely on the compiler (in NQP) to
generate the appropriate loading/installing code.

core/bytecode.c excerpt - nqp::extop_install:

```C
#include "moarvm.h"

typedef struct _MVMExtOpRecord {

    /* name of the op, including namespace:: prefix */
    MVMString *opname;

    /* string representing signature */
    MVMString *signature;

    /* the function pointer (see below for signature/macro) */
    MVMCustomOp *function_ptr;

    /* number of bytes the interpreter should advance the cur_op pointer */
    MVMint32 op_size;

    /* (speculative/future) function pointer to the code in C
        that the JIT can call to generate an AST for the
        operation, for super-ultra-awesome optimization
        possibilities (when pigs fly! ;) */
    /* MVMCustomOpJITtoMAST * jittomast_ptr; */

    /* so the record can be in a hash too (so a compiler or JIT
        can access the upper code at runtime in order to inline
        or optimize stuff) */
    UT_hash_handle hash_handle;
} MVMExtOpRecord;

/* Resolve the function pointer and nstall the op at runtime. */
void MVM_bytecode_extop_install(MVMThreadContext *tc, MVMObject *library,
        MVMString *opname, MVMString *funcname, MVMString *signature) {

    /* TODO: protect with a mutex */
    /* must also grab thread creation mutex b/c we have to
       update the tc->interp_customops pointer of all the threads */

    MVMCustomOp *function_ptr = NULL;
    MVMExtOpRecord *customops, *customop;
    MVMuint16 opidx = tc->instance->nextcustomop++;
    void *kdata;
    size_t klen;

    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, opname, "bad String");
    HASH_FIND(hash_handle, tc->instance->customops_hash, kdata, klen, customop);
    if (customop)
        MVM_panic(tc, "already installed custom op by this name");

    customops = tc->instance->customops;

    if (customops == NULL) {
        customops = tc->instance->customops =
            calloc( sizeof(MVMExtOpRecord),
                (tc->instance->customops_size = 256));
    }
    else if (opidx == tc->instance->customops_size) {
        customops = tc->instance->customops =
            realloc(tc->instance->customops,
                (tc->instance->customops_size *= 2));
        memset(tc->instance->customops + tc->instance->customops_size/2,
            0, tc->instance->customops_size / 2 * sizeof(MVMExtOpRecord));
    }

    customop = customops + opidx;
    customop->opname = opname;
    customop->signature = signature;
    customop->op_size = MVM_bytecode_extop_compute_opsize(tc, signature);

    /* use the NativeCall API directly to grab the function pointer
       using the cached library object */
    customop->function_ptr =
        MVM_nativecall_function_ptr(tc, library, funcname);

    /* the name strings should always be in a string heap already,
       so don't need GC root */
    HASH_ADD_KEYPTR(hash_handle, tc->instance->customops_hash, kdata, klen,
            customop);
}
```

core/interp.c excerpt - the invocation of nqp::customopcall's replacements:

```C
MVMExtOpRecord *customops = tc->instance->customops;
tc->interp_customops = &customops;

#define EXTOP_OFFSET 4096

<snip>

case MVM_OP_BANK_16:
case MVM_OP_BANK_17:
...
case MVM_OP_BANK_126:
case MVM_OP_BANK_127:
{
    MVMExtOpRecord *op_record =
            &customops[*(MVMuint16 *)cur_op++ - EXTOP_OFFSET];
    MVMCustomOp *function_ptr = op_record->function_ptr;
    function_ptr(tc);
    cur_op += op_record->op_size;
    break;
}
```

example extension (loading the rakudo ops dynamically) - Rakudo/Ops.p6 (or NQP):

```Perl
package Rakudo::Ops;

INIT {
  use MoarVM::CustomOps;
  install_ops('rakudo_ops.lib', 'MVM_rakudo_op_', 'rakudo', [
    'additivitation', 'iii',
    'concatenationize', 'sss',
  ]);
}

# Both at compile-time and run-time of the below code, INIT will have run
# and the following ops are installed the right namespaces and such.

my $z = rakudo::concatenationize(rakudo::additivitation(44, 66), "blah");

# note: since the types of the custom ops' operands are known to the
# HLL compiler, it just does its normal thing of generating code to
# auto-coerce the resulting integer from the addition to a string
# for the concat custom op.
```

moarvm.h excerpt (note the injecting of 1 offset if it's not the result reg):

```C
#define REG(idx) \
    (reg_base[*((MVMuint16 *)(cur_op + ((idx) > 0 ? idx + 1 : 0)))])

```

Note: The type checks should be compile-time optimized-away by all but the
stupidest of C compilers.  Though they fail at runtime, I consider that
"fail fast" enough, as this is simply a best-effort attempt at a coder
convenience type-check, not a rigorous one to actually enforce that the
register type signature passed to the runtime opcode installation routine
in the HLL code actually matches the one defined/used in the C source code.

moarvm.h excerpt (continued):

```C

#define MVM_CUSTOM_OP(opname, block) \
\
void opname(MVMThreadContext *tc) { \
    MVMuint8 *cur_op = *tc->interp_cur_op; \
    MVMRegister *reg_base = *tc->interp_reg_base; \
    MVMCompUnit *cu = *tc->interp_cu; \
    block; \
}
typedef MVM_CUSTOM_OP((*MVMCustomOp));
```

rakudo_ops.c

```C
#include "moarvm.h"

MVM_CUSTOM_OP(MVM_rakudo_op_additivitation, {
    REG(0).i = REG(1).i + REG(2).i;
})

MVM_CUSTOM_OP(MVM_rakudo_op_concatenationize, {
    REG(0).s = MVM_string_concatenate(tc, REG(1).s, REG(2).s);
})
```

validation.c excerpt (verify extop arg types and inline the real
oprecord offsets):

```C
/* similar to the actual interpreter, grab the MVMExtOpRecord, but
simply validate each operand type specified for the extop with
the types and count of the registers specified in the bytecode, by
enumerating each character in the signature. If
it hasn't been checked already, compare the signature of the loaded
extop by that name against the signature of the extop by that
name that was stored in the compilation unit when it was loaded from
disk, if it was.  Cache the function pointer if it wasn't already. */
```
