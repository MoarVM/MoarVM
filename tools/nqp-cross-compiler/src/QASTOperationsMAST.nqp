use QAST;
use MASTNodes;
use MASTOps;

my $MVM_operand_literal     := 0;
my $MVM_operand_read_reg    := 1;
my $MVM_operand_write_reg   := 2;
my $MVM_operand_read_lex    := 3;
my $MVM_operand_write_lex   := 4;
my $MVM_operand_rw_mask     := 7;

# the register "kind" codes, for expression results and arguments.
my $MVM_reg_void            := 0; # not really a register; just a result/return kind marker
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
    has $!result_kind;
    
    method new(:@instructions!, :$result_reg!, :$result_kind!) {
        my $obj := nqp::create(self);
        nqp::bindattr($obj, MAST::InstructionList, '@!instructions', @instructions);
        nqp::bindattr($obj, MAST::InstructionList, '$!result_reg', $result_reg);
        nqp::bindattr($obj, MAST::InstructionList, '$!result_kind', $result_kind);
        $obj
    }
    
    method instructions() { @!instructions }
    method result_reg()   { $!result_reg }
    method result_kind()  { $!result_kind }
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
        nqp::die("No registered operation handler for '$name'");
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
        my $result_kind := $MVM_reg_void;
        my $result_reg := MAST::VOID;
        my $needs_write := 0;
        my $type_var_kind := 0;
        
        my @arg_regs;
        my @all_ins;
        my @release_regs;
        my @release_kinds;
        
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
            my $operand := @operands[$operand_num++];
            my $arg_kind := $arg.result_kind;
            
            # args cannot be void
            if $arg_kind == $MVM_reg_void {
                nqp::die("Cannot use a void register as an argument to op '$op'");
            }
            
            my $operand_kind := ($operand +& $MVM_operand_type_mask);
            
            if ($operand_kind == $MVM_operand_type_var) {
                # handle ops that have type-variables as operands
                if ($type_var_kind) {
                    # if we've already seen a type-var
                    if ($arg_kind != $type_var_kind) {
                        # the arg types must match
                        nqp::die("variable-type op requires same-typed args");
                    }
                }
                else {
                    # set this variable-type op's typecode
                    $type_var_kind := $arg_kind;
                }
            }
            elsif ($arg_kind * 8 != $operand_kind) {
                # the arg typecode left shifted 3 must match the operand typecode
                nqp::die("arg type $arg_kind does not match operand type $operand_kind to op '$op'");
            }
            
            # if this is the write register, get the result reg and type from it
            if (($operand +& $MVM_operand_rw_mask) == $MVM_operand_write_reg
                || ($operand +& $MVM_operand_rw_mask) == $MVM_operand_write_lex) {
                $result_reg := $arg.result_reg;
                $result_kind := $arg_kind;
            }
            # otherwise it's a read register, so it can be released if it's an
            # intermediate value
            else {
                # if it's not a write register, queue it to be released it to the allocator
                nqp::push(@release_regs, $arg.result_reg);
                nqp::push(@release_kinds, $arg_kind);
            }
            
            # put the arg exression's generation code in the instruction list
            nqp::splice(@all_ins, $arg.instructions, +@all_ins, 0);
            nqp::push(@arg_regs, $arg.result_reg);
        }
        
        # release the registers to the allocator. See comment there.
        my $release_i := 0;
        $*REGALLOC.release_register($_, @release_kinds[$release_i++]) for @release_regs;
        
        # unshift in a generated write register arg if it needs one
        if ($needs_write) {
            # do this after the args to possibly reuse a register,
            # and so we know the type of result register for ops with type_var operands.
            
            $result_kind := (@operands[0] +& $MVM_operand_type_mask) / 8;
            
            # fixup the variable typecode if there is one
            if ($type_var_kind && $result_kind == $MVM_operand_type_var / 8) {
                $result_kind := $type_var_kind;
            }
            
            $result_reg := $*REGALLOC.fresh_register($result_kind);
            
            nqp::unshift(@arg_regs, $result_reg);
        }
        
        # Add operation node.
        nqp::push(@all_ins, MAST::Op.new(
            :bank(nqp::substr($bank, 1)), :op($op),
            |@arg_regs));
        
        # Build instruction list.
        MAST::InstructionList.new(@all_ins, $result_reg, $result_kind)
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
    QAST::MASTOperations.add_core_op($op_name, -> $qastcomp, $op {
        # Check operand count.
        my $operands := +$op.list;
        nqp::die("Operation '$op_name' needs either 2 or 3 operands")
            if $operands < 2 || $operands > 3;
        
        # Create labels.
        my $if_id    := $qastcomp.unique($op_name);
        my $else_lbl := MAST::Label.new($if_id ~ '_else');
        my $end_lbl  := MAST::Label.new($if_id ~ '_end');
        
        # Compile each of the children
        my @comp_ops;
        @comp_ops.push($qastcomp.as_mast($_)) for $op.list;
        
        if (@comp_ops[0].result_kind == $MVM_reg_void) {
            nqp::die("operation '$op_name' condition cannot be void");
        }
        
        # XXX coerce one to match the other? how to choose which one?
        if ($operands == 3 && @comp_ops[1].result_kind != @comp_ops[2].result_kind
         || $operands == 2 && @comp_ops[0].result_kind != @comp_ops[1].result_kind) {
            nqp::die("For now, operation '$op_name' needs both branches to result in the same kind");
        }
        my $res_kind := @comp_ops[1].result_kind;
        my $is_void := $res_kind == $MVM_reg_void;
        my $res_reg  := $is_void ?? MAST::VOID !! $*REGALLOC.fresh_register($res_kind);
        
        my @ins;
        
        # Evaluate the condition first; store result if needed.
        push_ilist(@ins, @comp_ops[0]);
        if $operands == 2 && !$is_void {
            push_op(@ins, 'set', $res_reg, @comp_ops[0].result_reg);
        }
        
        # Emit the jump.
        push_op(@ins,
            resolve_condition_op(@comp_ops[0].result_kind, $op_name eq 'if'),
            @comp_ops[0].result_reg,
            ($operands == 3 ?? $else_lbl !! $end_lbl)
        );
        $*REGALLOC.release_register(@comp_ops[0].result_reg, @comp_ops[0].result_kind);
        
        # Emit the then, stash the result
        push_ilist(@ins, @comp_ops[1]);
        push_op(@ins, 'set', $res_reg, @comp_ops[1].result_reg) unless $is_void;
        $*REGALLOC.release_register(@comp_ops[1].result_reg, @comp_ops[1].result_kind);
        
        # Handle else branch if needed.
        if $operands == 3 {
            push_op(@ins, 'goto', $end_lbl);
            nqp::push(@ins, $else_lbl);
            # XXX see coercion note above
            push_ilist(@ins, @comp_ops[2]);
            push_op(@ins, 'set', $res_reg, @comp_ops[2].result_reg) unless $is_void;
            $*REGALLOC.release_register(@comp_ops[2].result_reg, @comp_ops[2].result_kind);
        }
        
        nqp::push(@ins, $end_lbl);
        
        # Build instruction list
        # XXX see coercion note above for result type
        MAST::InstructionList.new(@ins, $res_reg, $res_kind)
    });
}

# Loops.
for <while until> -> $op_name {
    QAST::MASTOperations.add_core_op($op_name, -> $qastcomp, $op {
        # Check operand count.
        my $operands := +$op.list;
        nqp::die("Operation '$op_name' needs 2 operands")
            if $operands != 2;
        
        # Create labels.
        my $while_id    := $qastcomp.unique($op_name);
        my $loop_lbl := MAST::Label.new($while_id ~ '_loop');
        my $last_lbl  := MAST::Label.new($while_id ~ '_last');
        
        # Compile each of the children
        my @comp_ops;
        @comp_ops.push($qastcomp.as_mast($_)) for $op.list;
        
        if (@comp_ops[0].result_kind == $MVM_reg_void) {
            nqp::die("operation '$op_name' condition cannot be void");
        }
        
        # XXX coerce one to match the other? how to choose which one?
        if (@comp_ops[0].result_kind != @comp_ops[1].result_kind) {
            nqp::die("For now, operation '$op_name' needs both branches to result in the same kind");
        }
        my $res_kind := @comp_ops[1].result_kind;
        my $res_reg  := $*REGALLOC.fresh_register($res_kind);
        
        my @ins;
        
        nqp::push(@ins, $loop_lbl);
        push_ilist(@ins, @comp_ops[0]);
        push_op(@ins, 'set', $res_reg, @comp_ops[0].result_reg);
        
        # Emit the exiting jump.
        push_op(@ins,
            resolve_condition_op(@comp_ops[0].result_kind, $op_name eq 'while'),
            @comp_ops[0].result_reg,
            $last_lbl
        );
        $*REGALLOC.release_register(@comp_ops[0].result_reg, @comp_ops[0].result_kind);
        
        # Emit the loop body; stash the result.
        push_ilist(@ins, @comp_ops[1]);
        push_op(@ins, 'set', $res_reg, @comp_ops[1].result_reg);
        $*REGALLOC.release_register(@comp_ops[1].result_reg, @comp_ops[1].result_kind);
        
        # Emit the iteration jump.
        push_op(@ins, 'goto', $loop_lbl);
        
        # Emit last label.
        nqp::push(@ins, $last_lbl);
        
        # Build instruction list
        # XXX see coercion note above for result type
        MAST::InstructionList.new(@ins, $res_reg, $res_kind)
    });
}

for <repeat_while repeat_until> -> $op_name {
    QAST::MASTOperations.add_core_op($op_name, -> $qastcomp, $op {
        # Check operand count.
        my $operands := +$op.list;
        nqp::die("Operation '$op_name' needs 2 operands")
            if $operands != 2;
        
        # Create labels.
        my $while_id := $qastcomp.unique($op_name);
        my $loop_lbl := MAST::Label.new($while_id ~ '_loop');
        
        # Compile each of the children
        my @comp_ops;
        @comp_ops.push($qastcomp.as_mast($_)) for $op.list;
        
        if (@comp_ops[1].result_kind == $MVM_reg_void) {
            nqp::die("operation '$op_name' condition cannot be void");
        }
        
        my $res_kind := @comp_ops[0].result_kind;
        my $res_reg  := $*REGALLOC.fresh_register($res_kind);
        
        my @ins;
        
        nqp::push(@ins, $loop_lbl);
        push_ilist(@ins, @comp_ops[0]);
        $*REGALLOC.release_register(@comp_ops[0].result_reg, @comp_ops[0].result_kind);
        
        # Emit the condition; stash the result.
        push_ilist(@ins, @comp_ops[1]);
        push_op(@ins, 'set', $res_reg, @comp_ops[1].result_reg);
        
        # Emit the looping jump.
        push_op(@ins,
            resolve_condition_op(@comp_ops[1].result_kind, $op_name eq 'repeat_until'),
            @comp_ops[1].result_reg,
            $loop_lbl
        );
        $*REGALLOC.release_register(@comp_ops[1].result_reg, @comp_ops[1].result_kind);
        
        # Build instruction list
        MAST::InstructionList.new(@ins, $res_reg, $res_kind)
    });
}

# Binding
QAST::MASTOperations.add_core_op('bind', -> $qastcomp, $op {
    # Sanity checks.
    my @children := $op.list;
    if +@children != 2 {
        pir::die("A 'bind' op must have exactly two children");
    }
    unless nqp::istype(@children[0], QAST::Var) {
        pir::die("First child of a 'bind' op must be a QAST::Var");
    }
    
    # Set the QAST of the think we're to bind, then delegate to
    # the compilation of the QAST::Var to handle the rest.
    my $*BINDVAL := @children[1];
    $qastcomp.as_mast(@children[0])
});

sub resolve_condition_op($kind, $negated) {
    return $negated ??
        $kind == $MVM_reg_int64 ?? 'unless_i' !!
        $kind == $MVM_reg_num64 ?? 'unless_n' !!
        $kind == $MVM_reg_str   ?? 'unless_s' !!
        $kind == $MVM_reg_obj   ?? 'unless_o' !!
        nqp::die("unhandled kind $kind")
     !! $kind == $MVM_reg_int64 ?? 'if_i' !!
        $kind == $MVM_reg_num64 ?? 'if_n' !!
        $kind == $MVM_reg_str   ?? 'if_s' !!
        $kind == $MVM_reg_obj   ?? 'if_o' !!
        nqp::die("unhandled kind $kind")
}

sub push_op(@dest, $op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);
    
    nqp::push(@dest, MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    ));
}

sub push_ilist(@dest, $src) {
    nqp::splice(@dest, $src.instructions, +@dest, 0);
}
