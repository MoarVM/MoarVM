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
    
    # Maps operations to code that will handle them. Hash of code.
    my %core_ops;
    
    # Maps HLL-specific operations to code that will handle them.
    # Hash of hash of code.
    my %hll_ops;
    
    ## Cached pirop compilers.
    #my %cached_pirops;
    
    # Compiles an operation to MAST.
    method compile_op($qastcomp, $hll, $op) {
        my $name := $op.op;
        if $hll {
            if %hll_ops{$hll} && %hll_ops{$hll}{$name} -> $mapper {
                return $mapper($qastcomp, $op);
            }
        }
        if %core_ops{$name} -> $mapper {
            return $mapper($qastcomp, $op);
        }
        pir::die("No registered operation handler for '$name'");
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
        
        my @arg_regs;
        my @all_ins;
        my @release_regs;
        my @release_types;
        
        # if the op has operands, and the first operand is a write register,
        # and the number of args provided is one less than the number of operands needed,
        # mark that we need to generate a result register at the end, and
        # advance to the second operand.
        if ($num_operands
                && (@operands[0] +& $MVM_operand_rw_mask) == $MVM_operand_write_reg
                    # allow the QASTree to define its own write register
                && $num_args == $num_operands - 1) {
            $needs_write := 1;
            $operand_num++;
        }
        
        if ($num_args != $num_operands - $operand_num) {
            nqp::die("Arg count doesn't equal required operand count for op '$op'");
        }
        
        # Compile provided args.
        for @args {
            my $arg := $qastcomp.as_mast($_);
            my $operand_type := @operands[$operand_num++];
            my $arg_type := $arg.result_type;
            
            my $arg_typecode := type_to_typecode($arg_type);
            
            # args cannot be void
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
            
            # if this is the write register, get the result reg and type from it
            if (($operand_type +& $MVM_operand_rw_mask) == $MVM_operand_write_reg
                || ($operand_type +& $MVM_operand_rw_mask) == $MVM_operand_write_lex) {
                $result_reg := $arg.result_reg;
                $result_type := $arg_type;
            }
            # otherwise it's a read register, so it can be released if it's an
            # intermediate value
            else {
                # if it's not a write register, queue it to be released it to the allocator
                nqp::push(@release_regs, $arg.result_reg);
                nqp::push(@release_types, $arg_typecode * 8);
            }
            
            # put the arg exression's generation code in the instruction list
            nqp::splice(@all_ins, $arg.instructions, +@all_ins, 0);
            nqp::push(@arg_regs, $arg.result_reg);
        }
        
        # release the registers to the allocator. See comment there.
        my $release_i := 0;
        for @release_regs {
            release_register($_, @release_types[$release_i++] / 8);
        }
        
        # unshift in a generated write register arg if it needs one
        if ($needs_write) {
            # do this after the args to possibly reuse a register,
            # and so we know the type of result register for ops with type_var operands.
            
            my $result_typecode := (@operands[0] +& $MVM_operand_type_mask);
            
            # fixup the variable typecode if there is one
            if ($type_var_typecode && $result_typecode == $MVM_operand_type_var) {
                $result_typecode := $type_var_typecode * 8;
            }
            
            $result_type := typecode_to_type($result_typecode / 8);
            $result_reg := typecode_to_register($result_typecode / 8);
            
            nqp::unshift(@arg_regs, $result_reg);
        }
        
        # Add operation node.
        nqp::push(@all_ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op),
            |@arg_regs));
        
        # Build instruction list.
        MAST::InstructionList.new(@all_ins, $result_reg, $result_type)
    }
    
    # Adds a core op handler.
    method add_core_op($op, $handler) {
        %core_ops{$op} := $handler;
    }
    
    # Adds a HLL op handler.
    method add_hll_op($hll, $op, $handler) {
        %hll_ops{$hll} := {} unless %hll_ops{$hll};
        %hll_ops{$hll}{$op} := $handler;
    }
    
}

# Conditionals.
for <if unless> -> $op_name {
    QAST::Operations.add_core_op(c, -> $qastcomp, $op {
        # Check operand count.
        my $operands := +$op.list;
        nqp::die("Operation '$op_name' needs either 2 or 3 operands")
            if $operands < 2 || $operands > 3;
        
        # Create labels.
        my $if_id    := $qastcomp.unique($op_name);
        my $else_lbl := MAST::Label.new($if_id ~ '_else');
        my $end_lbl  := MAST::Label.new($if_id ~ '_end');
        
        
        # Compile each of the children; we'll need to look at the result
        # types and pick an overall result type if in non-void context.
        my @comp_ops;
        my @op_types;
        for $op.list {
            my $comp := $qastcomp.as_post($_);
            @comp_ops.push($comp);
            @op_types.push(type_to_typecode($comp.result_type));
        }
        # XXX coerce one to match the other? how to choose which one?
        if ($operands == 3 && @op_types[1] != @op_types[2]
         || $operands == 2 && @op_types[0] != @op_types[1]) {
            nqp::die("For now, operation '$op_name' needs both branches to result in the same type");
        }
        my $res_type := @op_types[1];
        my $res_reg  := typecode_to_register($res_type);
        
        my @ins;
        
        # Evaluate the condition first; store result if needed.
        push_ilist(@ins, @comp_ops[0]);
        if $operands == 2 {
            push_op(@ins, 'set', $res_reg, @comp_ops[0].result_reg);
        }
        
        # Emit the jump.
        push_op(@ins,
            resolve_condition_op($res_type, $op_name eq 'if'),
            @comp_ops[0].result_reg,
            $else_lbl
        );
        release_register(@comp_ops[0].result_reg, @op_types[0]);
        
        # Emit the then, stash the result
        push_ilist(@ins, @comp_ops[1]);
        push_op(@ins, 'set', $res_reg, @comp_ops[1].result_reg);
        release_register(@comp_ops[1].result_reg, @op_types[1]);
        
        # Handle else branch if needed.
        if $operands == 3 {
            push_op(@ins, 'goto', $end_lbl);
            nqp::push(@ins, $else_lbl);
            push_ilist(@ins, @comp_ops[2]);
            push_op(@ins, 'set', $res_reg, @comp_ops[2].result_reg);
            release_register(@comp_ops[2].result_reg, @op_types[2]);
        }
        else {
            nqp::push(@ins, $else_lbl);
        }
        
        # Build instruction list
        MAST::InstructionList.new(@ins, $res_reg, @comp_ops[1].result_type)
    });
}

sub resolve_condition_op($type, $negated) {
    return $negated ??
        $type == $MVM_reg_int64 ?? 'unless_i' !!
        $type == $MVM_reg_num64 ?? 'unless_n' !!
        $type == $MVM_reg_str   ?? 'unless_s' !!
        $type == $MVM_reg_obj   ?? 'unless_o' !!
        nqp::die("unhandled typecode $type")
     !! $type == $MVM_reg_int64 ?? 'if_i' !!
        $type == $MVM_reg_num64 ?? 'if_n' !!
        $type == $MVM_reg_str   ?? 'if_s' !!
        $type == $MVM_reg_obj   ?? 'if_o' !!
        nqp::die("unhandled typecode $type")
}

sub push_op(@dest, $op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("Unable to resolve MAST op '$op'") unless $bank;
    
    nqp::push(@dest, MAST::Op.new(
        :bank($bank), :op($op),
        |@args
    ));
}

sub push_ilist(@dest, $src) {
    nqp::splice(@dest, $src.instructions, +@dest, 0);
}

# below, "type" means type literal,
# "typecode" means MVM_reg_*

my @prim_to_reg := [$MVM_reg_obj, $MVM_reg_int64, $MVM_reg_num64, $MVM_reg_str];
sub type_to_typecode($type) {
    @prim_to_reg[pir::repr_get_primitive_type_spec__IP($type)]
}

my @typecodes_to_types := [int, int, int, int, int, num, num, str, NQPMu];
sub typecode_to_type($typecode) {
    @typecodes_to_types[$typecode];
}

sub typecode_to_register($typecode) {
    if $typecode == $MVM_reg_int64 { return $*REGALLOC.fresh_i() }
    if $typecode == $MVM_reg_num64 { return $*REGALLOC.fresh_n() }
    if $typecode == $MVM_reg_str   { return $*REGALLOC.fresh_s() }
    if $typecode == $MVM_reg_obj   { return $*REGALLOC.fresh_o() }
    nqp::die("unhandled typecode $typecode");
}

sub type_to_register($type) {
    typecode_to_register(type_to_typecode($type))
}

sub release_register($reg, $typecode) {
    # XXX TODO !!!!! Don't release the register if it's a local Var
    if $typecode == $MVM_reg_int64 { return $*REGALLOC.release_i($reg) }
    if $typecode == $MVM_reg_num64 { return $*REGALLOC.release_n($reg) }
    if $typecode == $MVM_reg_str   { return $*REGALLOC.release_s($reg) }
    if $typecode == $MVM_reg_obj   { return $*REGALLOC.release_o($reg) }
    nqp::die("unhandled reg type $typecode");
}
