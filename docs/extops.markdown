# Extensions and Custom Ops [proposed/tentative]

Jonathan (and others) - please review/comment/fix/destroy/redo/etc

The MoarVM interpreter uses 16-bit opcodes, where the first byte is called
the "bank".  The first 128 banks are reserved for the MoarVM built-in opcodes
and semi-officially related software such as Rakudo Perl 6.  The other 128
banks are used for custom ops installed at runtime.  The last 8 banks (1024
codes) of the first 128 banks are reserved for "position-independent" codes
that will be translated upon loading of the bytecode, as they will be
registered with the in-compilation World upon loading of the compilation
unit.

The *actual* codes of opcodes (as they are seen by the interpreter runloop)
are not cached offline or on disk in a central registry.  Instead, the
opcodes *installed* by a particular compilation unit (generally, a .moarvm
bytecode file loaded from disk or a compilation unit generated in memory by
a compiler) lie within the opcodes in the 8 banks at banks 120-127 in the
in-memory and on-disk representations of the compilation unit bytecode blob.
This is accomplished by the "load" entry point of the compilation unit
running code that registers (up to 1024) of its "position-independent" codes
by name/namespace with the running compiler (as there is generally a World-
aware HLL compiler running at the time ModuleLoader does its thing).  When
it registers the new opcodes, each one is given the next available "real"
opcode in the upper 128 banks, and the mapping is returned to the running
compiler itself so that the compiler can store it as it chooses.

The below example purports to show the contents of a skeleton extension as
it would look to a developer.  (please excuse incorrect syntax; it's pretty
much pseudo-code.)  Note: the real extensions for Rakudo itself will
actually register and use opcodes from its own reserved banks in the lower
128, let's say banks 16-19, so this example is not quite representative in
that regard.

helper package (part of the MoarVM/NQP runtime) - MoarVM/CustomOps.p6:

    package MoarVM::CustomOps;
    use NativeCall;
    
    sub install_ops($library_file, $c_prefix,
            $op_prefix, %ops, $names_sigs) is export {
        my $world = nqp::hllcompilerworld;
        my $opslib = native_lib($library_file);
        -> $name, $sig {
            %_ops{$name} = $world.install_native_op("$op_prefix::$name",
                native_function($opslib, "$c_prefix$name"), $sig);
        } for $names_sigs;
    }

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
register signature (a la parrot's signature), so the bytecode verifier knows
how to validate its register arg types.

core/bytecode.c - nqp::installop (or moarvm::installop if it's "loaded"?): 

    #include "moarvm.h"
    
    /* returns the opcode (32768-65535) into which it was installed */
    MVMint64 MVM_bytecode_install_op(MVMThreadContext *tc, MVMString *opname,
            MVMint64 

example extension (loading the rakudo ops dynamically) - Rakudo/Ops.p6 (or NQP):

    package Rakudo::Ops;
    my %_ops;
    BEGIN {
      use MoarVM::CustomOps;
      install_ops('rakudo_ops.lib', 'MVM_rakudo_op_',
          'rakudo', (%_ops = nqp::hash), [
        'additivitation', 'iii',
        'concatenationize', 'sss',
      ]);
    }
    
    my $z = concatenationize(Rakudo::Ops::additivitation(44, 66), "blah");

moarvm.h excerpt

    #define _dq "
    #define REG(idx) (((idx) >= 0 && (idx) <= num_args) \
        ? reg_base[*((MVMuint16 *)(cur_op + (idx)))] \
        : MVM_panic(tc, "register index %i out of range (%i registers).", (idx), num_regs))
    #define IREG 1
    #define NREG 2
    #define SREG 3
    #define OREG 4

The type checks should be compile-time optimized-away by all but the
stupidest of C compilers.  Though they fail at runtime, I consider that
"fail fast" enough, as this is simply a best-effort attempt at a coder
convenience type-check, not a rigorous one to actually enforce that the
register type signature passed to the runtime opcode installation routine
in the HLL code actually matches the one defined/used in the C source code.

moarvm.h excerpt (continued):

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
    void opname(MVMThreadContext *tc, MVMFrame *frame) { \
        MVMuint8 *cur_op = *tc->interp_cur_op; \
        MVMRegister *reg_base = *tc->interp_reg_base; \
        MVMCompUnit *cu = *tc->interp_cu; \
        MVMuint8 num_args = (arity); \
        MVMuint8 reg0_type = (reg0);
        MVMuint8 reg1_type = (reg1);
        MVMuint8 reg2_type = (reg2);
        MVMuint8 reg3_type = (reg3);
        MVMuint8 reg4_type = (reg4);
        REGI(0) = 0; REGN(0) = 0.0; REGS(0) = REGO(0) = NULL; \
        block; \
    }
    typedef MVM_CUSTOM_OP((*MVMCustomOp));

rakudo_ops.c

    #include "moarvm.h"
    
    MVM_CUSTOM_OP(MVM_rakudo_op_additivitation, 2, { IREG, IREG, IREG }, {
        REGI(0) = REGI(1) + REGI(2);
    })
    
    MVM_CUSTOM_OP(MVM_rakudo_op_concatenationize, 2, { SREG, SREG, SREG }, {
        REGS(0) = MVM_string_concatenate(tc, REGS(1), REGS(2));
    })

Note: instead of listing each "xsub" op to be "imported" in the .p6
module's BEGIN block (to be passed to install_ops), the .c could also
contain an autoloader/importer routine that returns an array of pointers
as p6ints and names as MVMStrings so that NativeCall wouldn't need to
search for each one... somewhat similarly to how Lua does it.  (This is
why the 




