use QAST;
use MASTNodes;
use MASTOps;

my $MVM_operand_literal     := 0;
my $MVM_operand_read_reg    := 1;
my $MVM_operand_write_reg   := 2;
my $MVM_operand_read_lex    := 3;
my $MVM_operand_write_lex   := 4;
my $MVM_operand_rw_mask     := 7;

my $MVM_reg_int8            := 1;
my $MVM_reg_int16           := 2;
my $MVM_reg_int32           := 3;
my $MVM_reg_int64           := 4;
my $MVM_reg_num32           := 5;
my $MVM_reg_num64           := 6;
my $MVM_reg_str             := 7;
my $MVM_reg_obj             := 8;

my $MVM_operand_int8        := ($MVM_reg_int8 * 8);
my $MVM_operand_int16       := ($MVM_reg_int16 * 8);
my $MVM_operand_int32       := ($MVM_reg_int32 * 8);
my $MVM_operand_int64       := ($MVM_reg_int64 * 8);
my $MVM_operand_num32       := ($MVM_reg_num32 * 8);
my $MVM_operand_num64       := ($MVM_reg_num64 * 8);
my $MVM_operand_str         := ($MVM_reg_str * 8);
my $MVM_operand_obj         := ($MVM_reg_obj * 8);
my $MVM_operand_ins         := (9 * 8);
my $MVM_operand_type_var    := (10 * 8);
my $MVM_operand_lex_outer   := (11 * 8);
my $MVM_operand_coderef     := (12 * 8);
my $MVM_operand_callsite    := (13 * 8);
my $MVM_operand_type_mask   := (15 * 8);


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
    
    my @prim_to_reg := [$MVM_reg_obj, $MVM_reg_int64, $MVM_reg_num64, $MVM_reg_str];
    sub type_to_register_type($type) {
        @prim_to_reg[pir::repr_get_primitive_type_spec__IP($type)]
    }
    
    my @typecode_to_arg := [int, int, int, int, int, num, num, str, NQPMu];
    sub typecode_to_argtype($typecode) {
        @typecode_to_arg[$typecode / 8];
    }
    
    sub typecode_to_register($typecode) {
        if $typecode == $MVM_operand_int64 { return $*REGALLOC.fresh_i() }
        if $typecode == $MVM_operand_num64 { return $*REGALLOC.fresh_n() }
        if $typecode == $MVM_operand_str   { return $*REGALLOC.fresh_s() }
        if $typecode == $MVM_operand_obj   { return $*REGALLOC.fresh_o() }
        nqp::die("unhandled typecode $typecode");
    }
    
    sub release_register($reg, $typecode) {
        if $typecode == $MVM_operand_int64 { return $*REGALLOC.release_i($reg) }
        if $typecode == $MVM_operand_num64 { return $*REGALLOC.release_n($reg) }
        if $typecode == $MVM_operand_str   { return $*REGALLOC.release_s($reg) }
        if $typecode == $MVM_operand_obj   { return $*REGALLOC.release_o($reg) }
        nqp::die("unhandled typecode $typecode");
    }
    
    # XXX This needs the coercion machinary
    # that is (not yet fully) implemented in the PIR version of QAST::Compiler,
    # and so on.
    method compile_mastop($qastcomp, $op, @args) {
        # Resolve the op.
        my $bank;
        for MAST::Ops.WHO {
            $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
        }
        nqp::die("Unable to resolve MAST op '$op'") unless $bank;
        
        my @operands := MAST::Ops.WHO{$bank}{$op}{"operands"};
        my $num_args := +@args;
        my $num_operands := +@operands;
        my $operand_num := 0;
        my $result_type := MAST::VOID;
        my $result_reg := MAST::VOID;
        my $needs_write := 0;
        my $type_var_typecode := 0;
        
        # Compile args.
        my @arg_regs;
        my @all_ins;
        my @release_regs;
        my @release_types;
        if ($num_operands
                && ((@operands[0] +& $MVM_operand_rw_mask) == $MVM_operand_write_reg
                    || (@operands[0] +& $MVM_operand_rw_mask) == $MVM_operand_write_lex)
                    # allow the QASTree to define its own write register
                && $num_args == $num_operands - 1) {
            $needs_write := 1;
            $operand_num++;
        }
        
        if ($num_args != $num_operands - $operand_num) {
            nqp::die("Arg count doesn't equal required operand count for op '$op'");
        }
        
        for @args {
            my $arg := $qastcomp.as_mast($_);
            my $operand_type := @operands[$operand_num++];
            my $arg_type := $arg.result_type;
            
            my $arg_typecode := type_to_register_type($arg_type);
            
            if $arg_typecode == $MVM_reg_obj && $arg_type ~~ MAST::VOID {
                nqp::die("Cannot use a void register as an argument to op '$op'");
            }
            
            my $operand_typecode := ($operand_type +& $MVM_operand_type_mask);
            
            if ($operand_typecode == $MVM_operand_type_var) {
                # handle ops that have type-variables as operands
                if ($type_var_typecode) {
                    # if we've already seen a type-var
                    if ($arg_typecode != $type_var_typecode) {
                        # the arg types must match
                        nqp::die("variable-type op requires same-typed args");
                    }
                }
                else {
                    # set this variable-type op's typecode
                    $type_var_typecode := $arg_typecode;
                }
            }
            elsif ($arg_typecode * 8 != $operand_typecode) {
                # the arg typecode left shifted 3 must match the operand typecode
                nqp::die("arg type $arg_typecode does not match operand type $operand_typecode to op '$op'");
            }
            
            if (($operand_type +& $MVM_operand_rw_mask) == $MVM_operand_write_reg
                || ($operand_type +& $MVM_operand_rw_mask) == $MVM_operand_write_lex) {
                $result_reg := $arg.result_reg;
                $result_type := $arg_type;
            }
            else {
                # XXX TODO !!!!! Don't release the register if it's a local Var
                # if it's not a write register, queue it to be released it to the allocator
                nqp::push(@release_regs, $arg.result_reg);
                nqp::push(@release_types, $arg_typecode * 8);
            }
            
            nqp::splice(@all_ins, $arg.instructions, +@all_ins, 0);
            nqp::push(@arg_regs, $arg.result_reg);
            
            
        }
        
        # release the registers to the allocator. See comment there.
        my $release_i := 0;
        for @release_regs {
            release_register($_, @release_types[$release_i++]);
            # say("op $op released arg result register with index: " ~ nqp::getattr_i($_, MAST::Local, '$!index'));
        }
        
        # unshift in the write register arg if it needs one
        if ($needs_write) {
            # do this after the args to possibly reuse a register,
            # and so we know the type of result register for ops with type_var operands.
            
            my $result_typecode := (@operands[0] +& $MVM_operand_type_mask);
            
            # fixup the variable typecode if there is one
            if ($type_var_typecode && $result_typecode == $MVM_operand_type_var) {
                $result_typecode := $type_var_typecode;
            }
            
            $result_type := typecode_to_argtype($result_typecode);
            $result_reg := typecode_to_register($result_typecode);
            
            nqp::unshift(@arg_regs, $result_reg);
        }
        
        # Add operation node.
        nqp::push(@all_ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op),
            |@arg_regs));
        
        # Build instruction list.
        MAST::InstructionList.new(@all_ins, $result_reg, $result_type)
    }
}
