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

class MAST::INT  { }
class MAST::NUM  { }
class MAST::STR  { }
class MAST::OBJ  { }


class QAST::MASTOperations {
    
    sub argtype_to_typecode($argtype) {
        if $argtype ~~ MAST::INT { return $MVM_operand_int64 }
        if $argtype ~~ MAST::NUM { return $MVM_operand_num64 }
        if $argtype ~~ MAST::STR { return $MVM_operand_str }
        if $argtype ~~ MAST::OBJ { return $MVM_operand_obj }
        if $argtype ~~ MAST::VOID { nqp::die("no typecode for VOID") }
        nqp::die("argtype NYI");
    }
    
    sub typecode_to_argtype($typecode) {
        if $typecode == $MVM_operand_int64 { return MAST::INT; }
        if $typecode == $MVM_operand_num64 { return MAST::NUM; }
        if $typecode == $MVM_operand_str   { return MAST::STR; }
        if $typecode == $MVM_operand_obj   { return MAST::OBJ; }
        nqp::die("unhandled typecode $typecode");
    }
    
    sub typecode_to_register($typecode) {
        if $typecode == $MVM_operand_int64 { return $*REGALLOC.fresh_i(); }
        if $typecode == $MVM_operand_num64 { return $*REGALLOC.fresh_n(); }
        if $typecode == $MVM_operand_str   { return $*REGALLOC.fresh_s(); }
        if $typecode == $MVM_operand_obj   { return $*REGALLOC.fresh_o(); }
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
        
        # Compile args.
        my @arg_regs;
        my @all_ins;
        if ($num_operands > 0 && (@operands[0] +& $MVM_operand_rw_mask) == $MVM_operand_write_reg) {
            
            my $result_typecode := (@operands[0] +& $MVM_operand_type_mask);
            $result_type := typecode_to_argtype($result_typecode);
            $result_reg := typecode_to_register($result_typecode);
            
            nqp::push(@arg_regs, $result_reg);
            
            $operand_num++;
        }
        
        if ($num_args != $num_operands - $operand_num) {
            nqp::die("Arg count doesn't equal required operand count for op '$op'");
        }
        
        for @args {
            my $arg := $qastcomp.as_mast($_);
            my $operand_type := @operands[$operand_num++];
            my $arg_type := $arg.result_type;
            
            if $arg_type ~~ MAST::VOID {
                nqp::die("Cannot use a void register as an argument to op '$op'");
            }
            
            my $arg_typecode := argtype_to_typecode($arg_type);
            my $operand_typecode := ($operand_type +& $MVM_operand_type_mask);
            
            if ($arg_typecode != $operand_typecode) {
                nqp::die("arg type does not match operand type to op '$op'");
            }
            
            nqp::splice(@all_ins, $arg.instructions, +@all_ins, 0);
            nqp::push(@arg_regs, $arg.result_reg);
        }
        
        # Add operation node.
        nqp::push(@all_ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op),
            |@arg_regs));
        
        # Build instruction list.
        MAST::InstructionList.new(@all_ins, $result_reg, $result_type)
    }
}
