### Extensions and Custom Ops [proposed/tentative]

anyone review/comment please...

#### The MoarVM Opcodes Overview

The MoarVM interpreter uses 16-bit opcodes, where the first byte is called
the "bank".  There are currently around 470 built-in ops, and I'd guess
there'll be around 500 once Rakudo's bootstrapped and passing spectest.

The interpreter loop currently dispatches a top level of switch/case by
bank, after reading the next byte in the stream, and then dispatches by
switch/case to the particular op in the bank by reading the next byte in the
stream.  It is not known whether the various C compilers compile these any
more efficiently than a flat switch/case of all 65536 potential opcodes with
all cases enumerated (in order).  However, I suggest that it would be more
efficient to read both bytes into an unsigned short and then do bit 
operations to split out the two bytes... or simply dispatch into one large
switch/case.  It's at least worth testing whether it's faster on some
platforms and compilers.

Many opcodes are self-contained (they don't call other C functions, and some
don't even make system calls), but lots and lots of them do call functions.
Since the ./moarvm binary is statically-linked (mostly), the link-time code
generation and optimization by modern compilers should do a pretty good job
of making such things optimal [please excuse the truism].  However, in the
case of dynamically loaded extensions to the VM that need to dynamically
load native libraries with a C ABI (nearly all native libraries have a build
that exposes such a thing), the function pointers must be resolved at
runtime after the library is loaded.  Perl 6's NativeCall module (using
dyncall on parrot) can load libraries by name and enumerate/locate entry
points and functions by name.

I propose to use the dyncall functionality to load MoarVM extensions and
resolve function pointers.  The following is a draft design/spec doc for how
that might look:

----------------------------------------------------------------

#### Representing Custom Op Calls on Disk and in Memory

In the .moarvm disk representation of the bytecode, an entry for each custom
op invoked from that compilation unit exists in a table in the bytecode.
Each entry contains: 1. a 16-bit index into the string heap representing the
fully-qualified (namespace included) name of the op, and 2. a 16-bit MoarVM
opcode representing the signature of the op at the time it was loaded before
persisting to bytecode, and 3. an optional 16-bit index into the string heap
to a string that represents the signature of the custom op if it doesn't fit
within the 261 provided permutations of types/arities.  In the in-memory
(deserialized) representation of the bytecode/compilation unit, each record
also has room to store the cache of the function pointer representing the
C function to which the op was resolved.

The custom op calls in the bytecode instruction stream are stored as a
particular "stand-in" opcode - nqp::customopcall, which simply serves as a
sentinel for the bytecode validator to do its load-time resolution of the
custom ops.  The MAST compiler enforces the customopcall to have something
like the following signature: w(`1) int16 r(`2) r(`3) r(`4) r(`5), except
the 3rd through 6th operands are optional.  The int16 (inlined 16-bit
literal (unsigned, actually) integer) operand is the offset into the
compunit's custom_ops table for the record representing all the calls from
that compilation unit to that particular custom op.

#### Loading Code That Calls Custom Ops

During bytecode validation (on-demand upon first invocation of a frame),
when the validator comes across an nqp::customopcall opcode, it locates the
appropriate record, and if the record's function call slot is NULL, it means
the function pointer hasn't been resolved, but also that the saved signature
hasn't yet been validated against the version of that opcode that has been
loaded by its dependencies (if it has!).  First the validator does a hash
lookup to see whether the custom op has been loaded (this also requires a
mutex protection, unless we're by then using a lock-free HLL hash for this),
then if it hasn't, it throws an exception.  If it has been loaded by a
dependency, "awesome!".  So then it compares the signatures of the calls in
the compunit whose frame is being validated against the signature of the
loaded op by that name, and if they match, "awesome!".  If not, throw a
bytecode validation exception: "custom call signature mismatch - your
dependency broke you."

Then the validator substitutes in the appropriate opcode corresponding to
the signature.  These 261 opcodes have names such as copc_i_iiii (takes four
integer read operands and writes one integer operand), or copc_o_ (takes no
read operands and writes an object operand (possibly NULL, in the case of
operations that don't need to return anything.  It's important for the op
to always at least pretend to return something, to keep the QAST->MAST
compiler sane.  If the opcode is exotic (has more than 4 read operands), it
gets the special opcode nqp::bigcustomopcall, whose signature looks like:
w(`1) int16(opcall index like normal), int16(literal inlined integer,
which represents the number of read operands), int16(literal inlined int16
value, which represents the type of first read operand), r(`1)(first read
operand), and then repeat the prior two operands as many times as necessary.

Once the opcode is substituted in the instruction stream, operand type
validation of the actual passed parameters (register indexes) proceeds
normally in the bytecode, using the new opcode's signature.  The validator
doesn't need to do anything with the function pointer... Conjecture: though
I suggest testing the efficiency improvement/degradation of inlining the
function pointer into each callsite.

#### Loading Extensions

When a compilation unit is loaded, its "load" entry point routine calls the
INIT block of the compilation unit (if it has one), and the INIT block does
something like the example below, registering the native function pointers
and their signatures with the runtime.  It communicates with the runtime via
the currently-compiling compiler (as there is generally a World-aware HLL
compiler running at the time ModuleLoader does its thing).

Note: There will *also* be a central registry (simply stored within the
MoarVM source code repo) of custom ops in popular/primary extensions (such
as Rakudo and MoarVM's native Perl 5 2-way interop/embedding extension), and
any other custom ops in "standard"-ish "CPAN" (et al.) extension
distributions whose names/signatures are committed to not changing once
registered (an important module can simply reserve a new opcode (out of the
65536 available (minus the 700 built-ins)) if it needs to change the
signature of an op, but it also must change the op name as part of the
deprecation.

Luckily, a single C function signature is sufficient for invoking custom
ops, since the entire runtime can be reached from the MVMThreadContext *tc,
passed as the only parameter, and it doesn't need to return a value.

It's strongly suggested that a compilation unit always install all of its
own custom ops in its own load routine entry point, to guarantee that if
any of the other frames is invoked the op will be loaded.

Note: Afaict, we actually don't need to make special provision in QAST/MAST
for custom ops under this scheme, since the compiler will actually have
already *run* the INIT-time code of a module it's compiling by the time it
encounters an invocation of a custom op in the module it's compiling.

#### Examples

## THE BELOW IS FROM AN EARLIER ITERATION OF THE PROPOSED DESIGN; ALL THE TEXT ABOVE SUPERSEDES THE BELOW EXAMPLES (including comments).  IT'S SOMEWHAT HELPFUL TOWARD INDICATING WHAT THEY'LL LOOK LIKE EVENTURALLY THOUGH.

The below example purports to show the contents of a skeleton extension as
it would look to a developer.  (please excuse incorrect syntax; it's pseudo-
code in some places. ;)

helper package (part of the MoarVM/NQP runtime) - MoarVM/CustomOps.p6:

```Perl
package MoarVM::CustomOps;
use NativeCall;

my %typemap = 'i', 1, 'n', 2, 's', 3, 'o', 4;

sub install_ops($library_file, $c_prefix,
        $op_prefix, %ops, $names_sigs) is export {
    my $world = nqp::hllcompilerworld;
    my $opslib = native_lib($library_file);
    -> $name, $sig {
        my @args; my $count;
        { my $code = %typemap{$_};
            nqp::die("you fail") unless $code >= 1 && $code <= 4;
            push @args, $code;
        } for $sig.comb;
        nqp::die("must have a result type") unless +@args;
        nqp::die("too many args") if +@args > 5;
        push(@args, 0) while +@args < 5;
        %_ops{$name} = $world.install_native_op("$op_prefix::$name",
            native_function($opslib, "$c_prefix$name"), @args[0],
            @args[1], @args[2], @args[3], @args[4]);
    } for $names_sigs;
}
```

Above, the nqp::hllcompilerworld op simply retrieves an appropriately named
dynamic lexical of the current in-flight World object.  That class will have
an HLL method named install_native_op, detailed below.

Notice the helper package uses NativeCall to find the function pointers via
the native_function (or whatever it's named) routine.  When each function
pointer is passed to the install_native_op method of the in-flight World
object, that method in the HLL compiler will pass the function pointer to
a special internal opcode (nqp::installop) that takes the opcode namespace::
into which to install it (similar to Perl 5's xsub installer), the name of
the op as it will appear in the HLL source code (namespace::opname), the int
representing the function pointer address, and a string representing the
register signature (a la parrot's op signatures), so the bytecode verifier
knows how to validate its register arg types.

Since the custom ops are resolved "by name" (sort of) upon bytecode loading,
we don't have to worry about Rakudo bootstrapping, since in order to install
the custom ops for Rakudo, we can simply rely on the compiler (in NQP) to
generate the appropriate loading/installing code.

core/bytecode.c excerpt - nqp::installop:

```C
#include "moarvm.h"

typedef struct _MVMCustomOpRecord {
    
    /* name of the op, including namespace:: prefix */
    MVMString *opname;
    
    /* number of arg registers (not counting result/output reg) */
    MVMuint8 arg_count;
    /* TODO; bit-pack the above and the below 6 fields. */
    
    /* arg types; see below for values (0-4); 0 is a sentinel
        meaning no more args. */
    MVMuint8 arg_0_type;
    MVMuint8 arg_1_type;
    MVMuint8 arg_2_type;
    MVMuint8 arg_3_type;
    MVMuint8 arg_4_type;
    
    /* index of the op in the runtime-installed op table
        (loadorder-dependent code) - actual opcode as seen
        by the interpreter runloop (after it's been subbed
        by the bytecode loader/verifier).  */
    MVMuint16 opcode;
    
    /* the function pointer (see below for signature/macro) */
    MVMCustomOp *function_ptr;
    
    /* (speculative/future) function pointer to the code in C
        that the JIT can call to generate an AST for the
        operation, for super-ultra-awesome optimization
        possibilities (when pigs fly! ;) */
    /* MVMCustomOpJITtoMAST * jittomast_ptr; */
    
    /* so the record can be in a hash too (so a compiler or JIT
        can access the upper code at runtime in order to inline
        or optimize stuff) */
    UT_hash_handle hash_handle;
} MVMCustomOpRecord;

/* returns the installed opcode. The HLL compiler can DTRT with it. */
MVMint64 MVM_bytecode_installop(MVMThreadContext *tc, MVMString *opname,
        MVMint64 function_ptr, MVMint64 arg_0_type, MVMint64 arg_1_type,
        MVMint64 arg_2_type, MVMint64 arg_3_type, MVMint64 arg_4_type) {
    
    /* TODO: protect with a mutex */
    /* must also grab thread creation mutex b/c we have to
       update the tc->interp_customops pointer of all the threads */
    
    MVMCustomOpRecord *customops, *customop;
    MVMuint16 opcode = tc->instance->nextcustomop++;
    MVMuint8 arg_count;
    void *kdata;
    size_t klen;
    
    if (opcode == 32768)
        MVM_panic(tc, "too many custom ops!");
    
    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, opname, "bad String");
    HASH_FIND(hash_handle, tc->instance->customops_hash, kdata, klen, customop);
    if (customop)
        MVM_panic(tc, "already installed custom op by this name");
    
    customops = tc->instance->customops;
    
    if (customops == NULL) {
        customops = tc->instance->customops =
            calloc( sizeof(MVMCustomOpRecord),
                (tc->instance->customops_size = 256));
    }
    else if (opcode == tc->instance->customops_size) {
        customops = tc->instance->customops =
            realloc(tc->instance->customops,
                (tc->instance->customops_size *= 2));
        memset(tc->instance->customops + tc->instance->customops_size/2,
            0, tc->instance->customops_size / 2 * sizeof(MVMCustomOpRecord));
    }
    
    customop = customops + opcode;
    customop->opname = opname;
    customop->arg_0_type = arg_0_type;
    arg_count = 1;
    if (customop->arg_1_type = arg_1_type) arg_count++;
    if (customop->arg_2_type = arg_2_type) arg_count++;
    if (customop->arg_3_type = arg_3_type) arg_count++;
    if (customop->arg_4_type = arg_4_type) arg_count++;
    customop->arg_count = arg_count;
    customop->function_ptr = (MVMCustomOp *)function_ptr; /* LOLZ; safe! */
    /* the name strings should always be in a string heap already, so don't need GC root */
    HASH_ADD_KEYPTR(hash_handle, tc->instance->customops_hash, kdata, klen, customop);
    
    /* TODO: find the last-initiated MVMCompUnit (should be the one that's
        running this installop), and work backwards from the literal string
        pointer provided for the opname to find its index in the string heap.
        Cache the opcode as a value in the offset of that index on
        compunit->strings_opcodes_map (array of struct { MVMuint16 index;
        MVMuint16 opcode; }), so that validation.c doesn't have to do a hash
        lookup to translate each custom op to its registered value. */
    
    /* TODO: this hash should eventually be a 6model Hash to 6model objects
        with these values in attributes, so the thing can be serialized as
        part of a preload-order bytecode ...load ... at which point this
        INIT-time registration simply becomes fixing up the function pointer
        and opcode value. */
    
    return customop->opcode = opcode + 32768;
}
```

core/interp.c excerpt - the invocation of nqp::customopcall's replacements:

```C
MVMCustomOpRecord *customops = tc->instance->customops;
tc->interp_customops = &customops;

<snip>

case MVM_OP_BANK_128:
case MVM_OP_BANK_129:
...
case MVM_OP_BANK_254:
case MVM_OP_BANK_255:
{
    MVMCustomOpRecord *op_record = &customops[*(MVMuint16 *)cur_op++ - 32678];
    MVMCustomOp *function_ptr = op_record->function_ptr;
    cur_op += 2 * (2 + op_record->arg_count);
    function_ptr(tc); /* function can get ->current_frame from tc */
}
```

example extension (loading the rakudo ops dynamically) - Rakudo/Ops.p6 (or NQP):

```Perl
package Rakudo::Ops;
my %_ops;
INIT {
  use MoarVM::CustomOps;
  install_ops('rakudo_ops.lib', 'MVM_rakudo_op_',
      'rakudo', (%_ops = nqp::hash), [
    'additivitation', 'iii',
    'concatenationize', 'sss',
  ]);
}

my $z = rakudo::concatenationize(rakudo::additivitation(44, 66), "blah");

# note: since the types of the custom ops' operands are known to the
# HLL compiler, it just does its normal thing of generating code to
# auto-coercing the resulting integer from the addition to a string
# for the concat custom op.
```

moarvm.h excerpt (note the injecting of 1 offset if it's not the result reg):

```C
#define _dq "
#define REG(idx) (((idx) >= 0 && (idx) <= num_args) \
    ? reg_base[*((MVMuint16 *)(cur_op + ((idx) > 0 ? idx + 1 : 0)))] \
    : MVM_panic(tc, "register index %i out of range (%i registers).", (idx), num_regs))

/* "B" is for "Blank"? */
#define BREG 0
#define IREG 1
#define NREG 2
#define SREG 3
#define OREG 4
```

Note: The type checks should be compile-time optimized-away by all but the
stupidest of C compilers.  Though they fail at runtime, I consider that
"fail fast" enough, as this is simply a best-effort attempt at a coder
convenience type-check, not a rigorous one to actually enforce that the
register type signature passed to the runtime opcode installation routine
in the HLL code actually matches the one defined/used in the C source code.

moarvm.h excerpt (continued):

```C
#define REGTYPECHECK(idx, type) ( \
    (type) >= IREG && (type) <= OREG && \
      (  ((idx) == 0 && (type) == arg0_type) \
      || ((idx) == 1 && (type) == arg1_type) \
      || ((idx) == 2 && (type) == arg2_type) \
      || ((idx) == 3 && (type) == arg3_type) \
      || ((idx) == 4 && (type) == arg4_type)) \
    ? REG((idx)) : MVM_panic(tc, "custom op argtype mismatch"))

#define REGI(idx) REGTYPECHECK((idx), IREG).i
#define REGN(idx) REGTYPECHECK((idx), NREG).n
#define REGS(idx) REGTYPECHECK((idx), SREG).s
#define REGO(idx) REGTYPECHECK((idx), OREG).o

#define MVM_CUSTOM_OP(opname, arity, block, reg0, reg1, reg2, reg3, reg4) \
\
void opname(MVMThreadContext *tc) { \
    MVMuint8 *cur_op = *tc->interp_cur_op; \
    MVMRegister *reg_base = *tc->interp_reg_base; \
    MVMCompUnit *cu = *tc->interp_cu; \
    MVMuint8 num_args = (arity); \
    MVMuint8 reg0_type = (reg0); \
    MVMuint8 reg1_type = (reg1); \
    MVMuint8 reg2_type = (reg2); \
    MVMuint8 reg3_type = (reg3); \
    MVMuint8 reg4_type = (reg4); \
    /* not sure the output result nulling is necessary */ \
    REGI(0) = 0; REGN(0) = 0.0; REGO(0) = REGS(0) = NULL; \
    block; \
}
typedef MVM_CUSTOM_OP((*MVMCustomOp));
```

rakudo_ops.c

```C
#include "moarvm.h"

MVM_CUSTOM_OP(MVM_rakudo_op_additivitation, 2, {
    REGI(0) = REGI(1) + REGI(2);
}, IREG, IREG, IREG, BREG, BREG)

MVM_CUSTOM_OP(MVM_rakudo_op_concatenationize, 2, {
    REGS(0) = MVM_string_concatenate(tc, REGS(1), REGS(2));
}, SREG, SREG, SREG, BREG, BREG)
```

Note: instead of listing each "xsub" op to be "imported" in the .p6
module's BEGIN block (to be passed to install_ops), the .c could also
contain an autoloader/importer routine that returns an array of pointers
as p6ints and names as MVMStrings so that NativeCall wouldn't need to
search for each one... somewhat similarly to how Lua does it.

validation.c excerpt (verify custom op arg types and inline the real
oprecord offsets):

```C
/* similar to the actual interpreter, grab the MVMCustomOpRecord, but
simply validate each operand type specified for the custom op with
the types and count of the registers specified in the bytecode. If
it hasn't been checked already, compare the signature of the loaded
custom op by that name against the signature of the custom op by that
name that was stored in the compilation unit when it was loaded from
disk, if it was.  Replace the nqp::customopcall opcode itself with
the opcode from the MVMCustomOpRecord.  Advance by the number of
operands, just like the interpreter. */
```

Conjecture: since we already have a record for each customop called
from a compilation unit, it actually doesn't save any instructions to
alter the opcode in the bytecode instruction stream in memory from
nqp::customopcall, since it has to lookup the 
