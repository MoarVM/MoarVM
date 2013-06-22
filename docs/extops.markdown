# Extensions and Custom Ops [proposed/tentative]

Jonathan (and others) - please review/comment/fix/destroy/redo/etc

The MoarVM interpreter uses 16-bit opcodes, where the first byte is called
the "bank".  The first 128 banks are reserved for the MoarVM built-in ops
and semi-officially related software such as Rakudo Perl 6.  The other 128
banks are used for custom ops installed at runtime.

The *actual* codes of opcodes (as they are seen by the interpreter runloop)
are not cached offline or on disk in a central registry.  Instead, in the
.moarvm disk representation of the bytecode, the custom op invocations are
translated (back) into their call-by-name variant, where the normal 16-bit
op is nqp::customopcall, and the 2nd arg to the op is a string heap offset
in that compilation unit to the name of the op it wants to call.  That is,
the "PIC" opcode is represented as a string by a layer of indirection via
the compilation unit's string heap.

During verification by the bytecode loader, those ops are translated into
their final form by running MVM_bytecode_customop_lookup, which looks in a
malloc'd cache table (of the same length as the compunit's string heap) of
those string heap offsets to an index of the MVMCustomOpRecord record below.
The inline size in the bytecode of each custom op invocation is not always
the same.  It consists of the 16-bit opcode itself (whether it's the
nqp::customopcall as stored on disk or the substituted actual op offset),
one register index for the result, one 16-bit inline int constant for the
string heap offset of the opname including namespace, and zero through four
register indexes for the op args.  It is up to the bytecode verifier to
check each invocation site against the signature registered with that op to
see if it matches the types of the register indexes in the bytecode.  The
bytecode verifier gets the signature information from the MVMCustomOpRecord.

When a compilation unit is loaded, its "load" entry point routine calls the
INIT block of the compilation unit (if it has one), and the INIT block does
something like the example below, registering the native function pointers
with the runtime.  It registers its "position-independent" codes by name
(including namespace::) with the running compiler (as there is generally a
World-aware HLL compiler running at the time ModuleLoader does its thing).
When it registers the new opcodes, each one is given the next available
"real" opcode in the upper 128 banks, and the compilation unit string heap
index (or some pre-mapping of it if that's not available yet) is returned
to the running compiler so it can inline it into the nqp::customopcall
bytecode as the 1st arg to the op call.

(deep breath)

The below example purports to show the contents of a skeleton extension as
it would look to a developer.  (please excuse incorrect syntax; it's pretty
much pseudo-code.)  Note: the extension(s) for the real Rakudo itself will
actually register and use opcodes from its own reserved banks in the lower
128, let's say banks 16-19, so this example is not quite representative in
that respect.

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
    REGI(0) = 0; REGN(0) = 0.0; REGS(0) = REGO(0) = NULL; \
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
/* pseudo-code, silly.
similar to the actual interpreter, grab the MVMCustomOpRecord, but
simply validate each operand type specified for the custom op with
the types and count of the registers specified in the bytecode.
Replace the opcode itself with the value in the first (constant int)
arg.  Advance by the number of operands, just like the interpreter. */
```

well.. there are a couple other moving parts I'm forgetting at the moment...
