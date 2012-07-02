use QAST;
use MASTNodes;
use MASTOps;

# This is used as a return value from all of the various compilation routines.
# It groups together a set of instructions along with a result register and a
# result type.
class MAST::InstructionList {
    has @!instructions;
    has $!result_reg;
    has $!result_type;
    
    method new(:@instructions!, :$result_reg!, :$result_type!) {
        my $obj := nqp::create(self);
        nqp::bindattr($obj, MAST::InstructionList, '@!instructions', @instructions);
        nqp::bindattr($obj, MAST::InstructionList, '$!result_reg', $result_reg);
        nqp::bindattr($obj, MAST::InstructionList, '$!result_type', $result_type);
        $obj
    }
    
    method instructions() { @!instructions }
    method result_reg()   { $!result_reg }
    method result_type()  { $!result_type }
}

# Marker object for void.
class MAST::VOID { }

class QAST::MASTOperations {
    # XXX This is a huge hack; needs type checking, the coercion machinary
    # that is (not yet fully) implemented in the PIR version of QAST::Compiler,
    # and so on. And that probably means our generated MASTOps lib needs to
    # expose the type information about ops to NQP land also. Also, it needs
    # to use $*REGALLOC in the case of any non-void op...and we need the op
    # info for that too.
    method compile_mastop($qastcomp, $op, @args) {
        # Resolve the op.
        my $bank;
        for MAST::Ops.WHO {
            $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
        }
        nqp::die("Unable to resolve MAST op '$op'") unless $bank;
        
        # Compile args.
        my @arg_regs;
        my @all_ins;
        for @args {
            my $arg := $qastcomp.as_mast($_);
            nqp::splice(@all_ins, $arg.instructions, +@all_ins, 0);
            nqp::push(@arg_regs, $arg.result_reg);
        }
        
        # Add operation node.
        nqp::push(@all_ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op),
            |@arg_regs));
        
        # Build instruction list.
        MAST::InstructionList.new(@all_ins, MAST::VOID, MAST::VOID)
    }
}
