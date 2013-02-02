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
# result kind.  It also tracks the source filename and line number.
class MAST::InstructionList {
    has @!instructions;
    has $!result_reg;
    has int $!result_kind;
    has str $!filename;
    has int $!lineno;
    
    method new(:@instructions!, :$result_reg!, :$result_kind!, :$filename = '<anon>', :$lineno = 0) {
        my $obj := nqp::create(self);
        nqp::bindattr($obj, MAST::InstructionList, '@!instructions', @instructions);
        nqp::bindattr($obj, MAST::InstructionList, '$!result_reg', $result_reg);
        nqp::bindattr_i($obj, MAST::InstructionList, '$!result_kind', $result_kind);
        nqp::bindattr_s($obj, MAST::InstructionList, '$!filename', $filename);
        nqp::bindattr_i($obj, MAST::InstructionList, '$!lineno', $lineno);
        $obj
    }
    
    method instructions() { @!instructions }
    method result_reg()   { $!result_reg }
    method result_kind()  { $!result_kind }
    method filename()     { $!filename }
    method lineno()       { $!lineno }
    
    method append(MAST::InstructionList $other) {
        push_ilist(@!instructions, $other);
        $!result_reg := $other.result_reg;
        $!result_kind := $other.result_kind;
    }
}

# Marker object for void.
class MAST::VOID { }

class QAST::MASTOperations {
    
    # Maps operations to code that will handle them. Hash of code.
    my %core_ops;
    
    # Maps HLL-specific operations to code that will handle them.
    # Hash of hash of code.
    my %hll_ops;
    
    # Mapping of how to box/unbox by HLL.
    my %hll_box;
    my %hll_unbox;
    
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
    
    my @kind_names := ['VOID','int8','int16','int32','int','num32','num','str','obj'];
    my @kind_types := [0,1,1,1,1,2,2,3,4];
    
    method compile_mastop($qastcomp, $op, @args, :$returnarg = -1, :$opname = 'none', :$want) {
        # Resolve the op.
        my $bank := 0;
        for MAST::Ops.WHO {
            next if ~$_ eq '$allops';
            $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
        }
        $op := $op.name if nqp::istype($op, QAST::Op);
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
            nqp::die("Arg count $num_args doesn't equal required operand count "~($num_operands - $operand_num)~" for op '$op'");
        }
        
        if ($op eq 'return') {
            $*BLOCK.return_kind($MVM_reg_void);
        }
        
        my $arg_num := 0;
        # Compile provided args.
        for @args {
            my $arg := $qastcomp.as_mast($_);
            my $operand := @operands[$operand_num++];
            my $constant_operand := !($operand +& $MVM_operand_rw_mask);
            my $arg_kind := $arg.result_kind;
            
            if $arg_num == 0 && nqp::substr($op, 0, 7) eq 'return_' {
                $*BLOCK.return_kind($arg.result_kind);
            }
            $arg_num++;
            
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
            } # allow nums and ints to be bigger than their destination width
            elsif (@kind_types[$arg_kind] != @kind_types[$operand_kind/8]) {
                $arg.append($qastcomp.coerce($arg, $operand_kind/8));
                $arg_kind := $operand_kind/8;
                # the arg typecode left shifted 3 must match the operand typecode
            #    nqp::die("arg type {@kind_names[$arg_kind]} does not match operand type {@kind_names[$operand_kind/8]} to op '$op'");
            }
            
            # if this is the write register, get the result reg and type from it
            if ($operand +& $MVM_operand_rw_mask) == $MVM_operand_write_reg
                || ($operand +& $MVM_operand_rw_mask) == $MVM_operand_write_lex
                || $returnarg != -1 && $returnarg == $arg_num {
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
            nqp::splice(@all_ins, $arg.instructions, +@all_ins, 0)
                unless $constant_operand;
            nqp::push(@arg_regs, $constant_operand
                ?? $qastcomp.as_mast_constant($_)
                !! $arg.result_reg);
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
    
    # Adds a core op that maps to a Moar op.
    method add_core_moarop_mapping($op, $moarop, $ret = -1, :$mapper?) {
        my $moarop_mapper := $mapper
            ?? $mapper(self, $moarop, $ret)
            !! self.moarop_mapper($moarop, $ret);
        %core_ops{$op} := -> $qastcomp, $op {
            $moarop_mapper($qastcomp, $op.op, $op.list)
        };
    }
    
    # Adds a HLL op that maps to a Moar op.
    method add_hll_moarop_mapping($hll, $op, $moarop, $ret = -1, :$mapper?) {
        my $moarop_mapper := $mapper
            ?? $mapper(self, $moarop, $ret)
            !! self.moarop_mapper($moarop, $ret);
        %hll_ops{$hll} := {} unless %hll_ops{$hll};
        %hll_ops{$hll}{$op} := -> $qastcomp, $op {
            $moarop_mapper($qastcomp, $op.op, $op.list)
        };
    }
    
    # Returns a mapper closure for turning an operation into a Moar op.
    # $ret is the 0-based index of which arg to use as the result when
    # the moarop is void.
    method moarop_mapper($moarop, $ret) {
        # do a little checking of input values
        
        # Resolve the op.
        my $bank;
        my $self := self;
        for MAST::Ops.WHO {
            $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $moarop);
        }
        nqp::die("Unable to resolve moarop '$moarop'") unless $bank;
        
        if $ret != -1 {
            my @operands := MAST::Ops.WHO{$bank}{$moarop}{"operands"};
            nqp::die("moarop $moarop return arg index out of range")
                if $ret < -1 || $ret >= +@operands;
            nqp::die("moarop $moarop is not void")
                if +@operands && (@operands[0] +& $MVM_operand_rw_mask) ==
                    $MVM_operand_write_reg;
        }
        
        -> $qastcomp, $op_name, @op_args {
            $self.compile_mastop($qastcomp, $moarop, @op_args,
                :returnarg($ret), :opname($op_name))
        }
    }
    
    # Adds a HLL box handler.
    method add_hll_box($hll, $type, $handler) {
        unless $type == $MVM_reg_int64 || $type == $MVM_reg_num64 || $type == $MVM_reg_str {
            nqp::die("Unknown box type '$type'");
        }
        %hll_box{$hll} := {} unless nqp::existskey(%hll_box, $hll);
        %hll_box{$hll}{$type} := $handler;
    }

    # Adds a HLL unbox handler.
    method add_hll_unbox($hll, $type, $handler) {
        unless $type == $MVM_reg_int64 || $type == $MVM_reg_num64 || $type == $MVM_reg_str {
            nqp::die("Unknown unbox type '$type'");
        }
        %hll_unbox{$hll} := {} unless nqp::existskey(%hll_unbox, $hll);
        %hll_unbox{$hll}{$type} := $handler;
    }

    # Generates instructions to box the result in reg.
    method box($qastcomp, $hll, $type, $reg) {
        (%hll_box{$hll}{$type} // %hll_box{''}{$type})($qastcomp, $reg)
    }

    # Generates instructions to unbox the result in reg.
    method unbox($qastcomp, $hll, $type, $reg) {
        (%hll_unbox{$hll}{$type} // %hll_unbox{''}{$type})($qastcomp, $reg)
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
        
        my $res_kind := @comp_ops[1].result_kind;
        my $is_void := $res_kind == $MVM_reg_void || (nqp::defined($*WANT) && $*WANT == $MVM_reg_void);
        my $res_reg  := $is_void ?? MAST::VOID !! $*REGALLOC.fresh_register($res_kind);
        
        my @ins;
        
        # Evaluate the condition first; store result if needed.
        push_ilist(@ins, @comp_ops[0]);
        if $operands == 2 && !$is_void {
            my $il := MAST::InstructionList.new(@ins, @comp_ops[0].result_reg, @comp_ops[0].result_kind);
            $qastcomp.coerce($il, $res_kind);
            push_op(@ins, 'set', $res_reg, $il.result_reg);
        }
        
        # Emit the jump.
        push_op(@ins,
            resolve_condition_op(@comp_ops[0].result_kind, $op_name eq 'if'),
            @comp_ops[0].result_reg,
            ($operands == 3 ?? $else_lbl !! $end_lbl)
        );
        
        # Emit the then, stash the result
        push_ilist(@ins, @comp_ops[1]);
        if (!$is_void && @comp_ops[1].result_kind != $res_kind) {
            my $coercion := $qastcomp.coercion(@comp_ops[1],
                (nqp::defined($*WANT) ?? $*WANT !! $MVM_reg_obj));
            push_ilist(@ins, $coercion);
            $*REGALLOC.release_register($res_reg, $res_kind);
            $res_reg := $*REGALLOC.fresh_register($coercion.result_kind);
            push_op(@ins, 'set', $res_reg, $coercion.result_reg);
            $res_kind := $coercion.result_kind;
        }
        elsif !$is_void {
            push_op(@ins, 'set', $res_reg, @comp_ops[1].result_reg);
        }
        $*REGALLOC.release_register(@comp_ops[1].result_reg, @comp_ops[1].result_kind);
        
        # Handle else branch (coercion of condition result if 2-arg).
        push_op(@ins, 'goto', $end_lbl);
        nqp::push(@ins, $else_lbl);
        if $operands == 3 {
            push_ilist(@ins, @comp_ops[2]);
        #    push_op(@ins, 'set', $res_reg, @comp_ops[2].result_reg) unless $is_void;
            $*REGALLOC.release_register(@comp_ops[2].result_reg, @comp_ops[2].result_kind);
            if !$is_void && @comp_ops[2].result_kind != $res_kind {
                my $coercion := $qastcomp.coercion(@comp_ops[2], $res_kind);
                push_ilist(@ins, $coercion);
                push_op(@ins, 'set', $res_reg, $coercion.result_reg);
            }
        }
        else {
            if !$is_void && @comp_ops[0].result_kind != $res_kind {
                my $coercion := $qastcomp.coercion(@comp_ops[0], $res_kind);
                push_ilist(@ins, $coercion);
                push_op(@ins, 'set', $res_reg, $coercion.result_reg);
            }
        }
        $*REGALLOC.release_register(@comp_ops[0].result_reg, @comp_ops[0].result_kind);
        nqp::push(@ins, $end_lbl);
        
        MAST::InstructionList.new(@ins, $res_reg, $res_kind)
    });
}

# Loops.
for <while until> -> $op_name {
    QAST::MASTOperations.add_core_op($op_name, -> $qastcomp, $op {
        # Check operand count.
        my $operands := +$op.list;
        nqp::die("Operation '$op_name' needs 2 or 3 operands; got $operands")
            if $operands != 2 && $operands != 3;
        
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
        
        if $operands == 3 {
            push_ilist(@ins, @comp_ops[2]);
        }
        
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
        
        my @ins;
        
        nqp::push(@ins, $loop_lbl);
        push_ilist(@ins, @comp_ops[0]);
        $*REGALLOC.release_register(@comp_ops[0].result_reg, @comp_ops[0].result_kind);
        
        # Emit the condition; stash the result.
        push_ilist(@ins, @comp_ops[1]);
        
        # Emit the looping jump.
        push_op(@ins,
            resolve_condition_op(@comp_ops[1].result_kind, $op_name eq 'repeat_until'),
            @comp_ops[1].result_reg,
            $loop_lbl
        );
        
        # Build instruction list
        MAST::InstructionList.new(@ins, @comp_ops[1].result_reg, @comp_ops[1].result_kind)
    });
}

# Binding
QAST::MASTOperations.add_core_op('bind', -> $qastcomp, $op {
    # Sanity checks.
    my @children := $op.list;
    if +@children != 2 {
        nqp::die("A 'bind' op must have exactly two children");
    }
    unless nqp::istype(@children[0], QAST::Var) {
        nqp::die("First child of a 'bind' op must be a QAST::Var");
    }
    
    # Set the QAST of the think we're to bind, then delegate to
    # the compilation of the QAST::Var to handle the rest.
    my $*BINDVAL := @children[1];
    $qastcomp.as_mast(@children[0])
});

my @kind_to_args := [0,
    $Arg::int,  # $MVM_reg_int8            := 1;
    $Arg::int,  # $MVM_reg_int16           := 2;
    $Arg::int,  # $MVM_reg_int32           := 3;
    $Arg::int,  # $MVM_reg_int64           := 4;
    $Arg::num,  # $MVM_reg_num32           := 5;
    $Arg::num,  # $MVM_reg_num64           := 6;
    $Arg::str,  # $MVM_reg_str             := 7;
    $Arg::obj   # $MVM_reg_obj             := 8;
];

# Calling.
sub handle_arg($arg, $qastcomp, @ins, @arg_regs, @arg_flags, @arg_kinds) {
    
    # generate the code for the arg expression
    my $arg_mast := $qastcomp.as_mast($arg);
    
    nqp::die("arg expression cannot be void")
        if $arg_mast.result_kind == $MVM_reg_void;
    
    nqp::die("arg code did not result in a MAST::Local")
        unless $arg_mast.result_reg && $arg_mast.result_reg ~~ MAST::Local;
    
    nqp::push(@arg_kinds, $arg_mast.result_kind);
    
    # append the code to the main instruction list
    push_ilist(@ins, $arg_mast);
    
    # build up the typeflag
    my $result_typeflag := @kind_to_args[$arg_mast.result_kind];
    if $arg.flat {
        $result_typeflag := $result_typeflag +| $Arg::flat;
        if $arg.named {
            # XXX flattened arg NYI
            $result_typeflag := $result_typeflag +| $Arg::named;
        }
    }
    elsif $arg.named -> $name {
        # add in the extra arg for the name
        nqp::push(@arg_regs, MAST::SVal.new( value => $name ));
        
        $result_typeflag := $result_typeflag +| $Arg::named;
    }
    
    # stash the result register and result typeflag
    nqp::push(@arg_regs, $arg_mast.result_reg);
    nqp::push(@arg_flags, $result_typeflag);
}

sub arrange_args(@in) {
    my @named := ();
    my @posit := ();
    for @in {
        nqp::push(((nqp::can($_, 'named') && $_.named) ?? @named !! @posit), $_);
    }
    for @named { nqp::push(@posit, $_) }
    @posit
}

QAST::MASTOperations.add_core_op('call', -> $qastcomp, $op {
    # Work out what callee is.
    my $callee;
    my @args := $op.list;
    if $op.name {
        $callee := $qastcomp.as_mast(QAST::Var.new( :name($op.name), :scope('lexical') ));
    }
    elsif +@args {
        $callee := $qastcomp.as_mast(@args.shift());
    }
    else {
        nqp::die("No name for call and empty children list");
    }
    @args := arrange_args(@args);
    
    nqp::die("callee expression must be an object")
        unless $callee.result_kind == $MVM_reg_obj;
    
    nqp::die("callee code did not result in a MAST::Local")
        unless $callee.result_reg && $callee.result_reg ~~ MAST::Local;
    
    # main instruction list
    my @ins := nqp::list();
    # the result MAST::Locals of the arg expressions
    my @arg_regs := nqp::list();
    # the result kind codes of the arg expressions
    my @arg_kinds := nqp::list();
    # the args' flags in the protocol the MAST compiler expects
    my @arg_flags := nqp::list();
    
    # Append the code to evaluate the callee expression
    push_ilist(@ins, $callee);
    
    # Process arguments.
    for @args {
        handle_arg($_, $qastcomp, @ins, @arg_regs, @arg_flags, @arg_kinds);
    }
    
    # Release the callee's result register
    $*REGALLOC.release_register($callee.result_reg, $MVM_reg_obj);
    
    # Release each arg's result register
    my $arg_num := 0;
    for @arg_regs -> $reg {
        if $reg ~~ MAST::Local {
            $*REGALLOC.release_register($reg, @arg_kinds[$arg_num]);
        }
        $arg_num++;
    }
    
    # Figure out result register type
    my $res_kind := $qastcomp.type_to_register_kind($op.returns);
    
    # and allocate a register for it. Probably reuse an arg's or the callee's.
    my $res_reg := $*REGALLOC.fresh_register($res_kind);
    
    # Generate call.
    nqp::push(@ins, MAST::Call.new(
        :target($callee.result_reg),
        :flags(@arg_flags),
        |@arg_regs,
        :result($res_reg)
    ));
    
    MAST::InstructionList.new(@ins, $res_reg, $res_kind)
});

QAST::MASTOperations.add_core_op('callmethod', -> $qastcomp, $op {
    my @args := $op.list;
    if +@args == 0 {
        nqp::die('Method call node requires at least one child');
    }
    my $invocant := $qastcomp.as_mast(@args.shift());
    my $methodname_expr;
    if $op.name {
        # great!
    }
    elsif +@args >= 1 {
        $methodname_expr := @args.shift();
    }
    else {
        nqp::die("Method call must either supply a name or have a child node that evaluates to the name");
    }
    @args := arrange_args(@args);
    
    nqp::die("invocant expression must be an object")
        unless $invocant.result_kind == $MVM_reg_obj;
    
    nqp::die("invocant code did not result in a MAST::Local")
        unless $invocant.result_reg && $invocant.result_reg ~~ MAST::Local;
    
    # main instruction list
    my @ins := [];
    # the result MAST::Locals of the arg expressions
    my @arg_regs := [$invocant.result_reg];
    # the result kind codes of the arg expressions
    my @arg_kinds := [$MVM_reg_obj];
    # the args' flags in the protocol the MAST compiler expects
    my @arg_flags := [$Arg::obj];
    
    # evaluate the invocant expression
    push_ilist(@ins, $invocant);
    
    # Process arguments.
    for @args {
        handle_arg($_, $qastcomp, @ins, @arg_regs, @arg_flags, @arg_kinds);
    }
    
    # generate and emit findmethod code
    my $callee_reg := $*REGALLOC.fresh_o();
    
    # This will hold the 3rd argument to findmeth(_s) - the method name
    # either a MAST::SVal or an $MVM_reg_str
    my $method_name;
    if $op.name {
        $method_name := MAST::SVal.new( :value($op.name) );
    }
    else {
        my $method_name_ilist := $qastcomp.as_mast($methodname_expr);
        # this check may not be necessary (enforced by the HLL grammar)
        nqp::die("method name expression must result in a string")
            unless $method_name_ilist.result_kind == $MVM_reg_str;
        push_ilist(@ins, $method_name_ilist);
        $method_name := $method_name_ilist.result_reg;
    }
    
    # push the op that finds the method based on either the provided name
    # or the provided name-producing expression.
    push_op(@ins, ($op.name ?? 'findmeth' !! 'findmeth_s'),
        $callee_reg, $invocant.result_reg, $method_name);
    
    # release the method name register if we used one
    $*REGALLOC.release_register($method_name, $MVM_reg_str) unless $op.name;
    
    # release the callee register
    $*REGALLOC.release_register($callee_reg, $MVM_reg_obj);
    
    # Release the invocant's and each arg's result registers
    my $arg_num := 0;
    for @arg_regs -> $reg {
        if $reg ~~ MAST::Local {
            $*REGALLOC.release_register($reg, @arg_kinds[$arg_num]);
        }
        $arg_num++;
    }
    
    # Figure out expected result register type
    my $res_kind := $qastcomp.type_to_register_kind($op.returns);
    
    # and allocate a register for it. Probably reuse an arg's or the invocant's.
    my $res_reg := $*REGALLOC.fresh_register($res_kind);
    
    # Generate call.
    nqp::push(@ins, MAST::Call.new(
        :target($callee_reg),
        :result($res_reg),
        :flags(@arg_flags),
        |@arg_regs
    ));
    
    MAST::InstructionList.new(@ins, $res_reg, $res_kind)
});

# say
my @say_opnames := [
    'say','say_i','say_i','say_i','say_i','say_n','say_n','say_s','say_o'
];
QAST::MASTOperations.add_core_moarop_mapping('say', 'n/a',
:mapper(-> $operations, $moarop, $ret {
    -> $qastcomp, $op_name, @op_args {
        my @ins := nqp::list();
        
        my $arg := $qastcomp.as_mast(@op_args[0]);
        nqp::splice(@ins, $arg.instructions, 0, 0);
        push_op(@ins, @say_opnames[$arg.result_kind], $arg.result_reg);
        MAST::InstructionList.new(@ins, $arg.result_reg, $arg.result_kind)
    }
}));

# arithmetic opcodes
QAST::MASTOperations.add_core_moarop_mapping('add_i', 'add_i');
#QAST::MASTOperations.add_core_moarop_mapping('add_I', 'nqp_bigint_add');
QAST::MASTOperations.add_core_moarop_mapping('add_n', 'add_n');
QAST::MASTOperations.add_core_moarop_mapping('sub_i', 'sub_i');
#QAST::MASTOperations.add_core_moarop_mapping('sub_I', 'nqp_bigint_sub', 'PPPP');
QAST::MASTOperations.add_core_moarop_mapping('sub_n', 'sub_n');
QAST::MASTOperations.add_core_moarop_mapping('mul_i', 'mul_i');
#QAST::MASTOperations.add_core_moarop_mapping('mul_I', 'nqp_bigint_mul', 'PPPP');
QAST::MASTOperations.add_core_moarop_mapping('mul_n', 'mul_n');
QAST::MASTOperations.add_core_moarop_mapping('div_i', 'div_i');
#QAST::MASTOperations.add_core_moarop_mapping('div_I', 'nqp_bigint_div', 'PPPP');
#QAST::MASTOperations.add_core_moarop_mapping('div_In', 'nqp_bigint_div_num', 'NPP');
QAST::MASTOperations.add_core_moarop_mapping('div_n', 'div_n');
QAST::MASTOperations.add_core_moarop_mapping('mod_i', 'mod_i');
#QAST::MASTOperations.add_core_moarop_mapping('mod_I', 'nqp_bigint_mod', 'PPPP');
#QAST::MASTOperations.add_core_moarop_mapping('expmod_I', 'nqp_bigint_exp_mod', 'PPPPP');
#QAST::MASTOperations.add_core_moarop_mapping('mod_n', 'mod_n');
QAST::MASTOperations.add_core_moarop_mapping('pow_n', 'pow_n');
#QAST::MASTOperations.add_core_moarop_mapping('pow_I', 'nqp_bigint_pow', 'PPPPP');
QAST::MASTOperations.add_core_moarop_mapping('neg_i', 'neg_i');
#QAST::MASTOperations.add_core_moarop_mapping('neg_I', 'nqp_bigint_neg', 'PPP');
QAST::MASTOperations.add_core_moarop_mapping('neg_n', 'neg_i');

QAST::MASTOperations.add_core_moarop_mapping('iseq_i', 'eq_i');
QAST::MASTOperations.add_core_moarop_mapping('isne_i', 'ne_i');
QAST::MASTOperations.add_core_moarop_mapping('islt_i', 'lt_i');
QAST::MASTOperations.add_core_moarop_mapping('isle_i', 'le_i');
QAST::MASTOperations.add_core_moarop_mapping('isgt_i', 'gt_i');
QAST::MASTOperations.add_core_moarop_mapping('isge_i', 'ge_i');
QAST::MASTOperations.add_core_moarop_mapping('iseq_n', 'eq_n');
QAST::MASTOperations.add_core_moarop_mapping('isne_n', 'ne_n');
QAST::MASTOperations.add_core_moarop_mapping('islt_n', 'lt_n');
QAST::MASTOperations.add_core_moarop_mapping('isle_n', 'le_n');
QAST::MASTOperations.add_core_moarop_mapping('isgt_n', 'gt_n');
QAST::MASTOperations.add_core_moarop_mapping('isge_n', 'ge_n');


#QAST::MASTOperations.add_core_moarop_mapping('abs_i', 'abs', 'Ii');
#QAST::MASTOperations.add_core_moarop_mapping('abs_I', 'nqp_bigint_abs', 'PPP');
#QAST::MASTOperations.add_core_moarop_mapping('abs_n', 'abs', 'Nn');

#QAST::MASTOperations.add_core_moarop_mapping('gcd_i', 'gcd', 'Ii');
#QAST::MASTOperations.add_core_moarop_mapping('gcd_I', 'nqp_bigint_gcd', 'PPP');
#QAST::MASTOperations.add_core_moarop_mapping('lcm_i', 'lcm', 'Ii');
#QAST::MASTOperations.add_core_moarop_mapping('lcm_I', 'nqp_bigint_lcm', 'PPP');

#QAST::MASTOperations.add_core_moarop_mapping('ceil_n', 'ceil', 'Nn');
#QAST::MASTOperations.add_core_moarop_mapping('floor_n', 'floor', 'NN');
#QAST::MASTOperations.add_core_moarop_mapping('ln_n', 'ln', 'Nn');
#QAST::MASTOperations.add_core_moarop_mapping('sqrt_n', 'sqrt', 'Nn');
#QAST::MASTOperations.add_core_moarop_mapping('radix', 'nqp_radix', 'Pisii');
#QAST::MASTOperations.add_core_moarop_mapping('radix_I', 'nqp_bigint_radix', 'PisiiP');
#QAST::MASTOperations.add_core_moarop_mapping('log_n', 'ln', 'NN');
#QAST::MASTOperations.add_core_moarop_mapping('exp_n', 'exp', 'Nn');
#QAST::MASTOperations.add_core_moarop_mapping('isnanorinf', 'is_inf_or_nan', 'In');

# trig opcodes
QAST::MASTOperations.add_core_moarop_mapping('sin_n', 'sin_n');
QAST::MASTOperations.add_core_moarop_mapping('asin_n', 'asin_n');
QAST::MASTOperations.add_core_moarop_mapping('cos_n', 'cos_n');
QAST::MASTOperations.add_core_moarop_mapping('acos_n', 'acos_n');
QAST::MASTOperations.add_core_moarop_mapping('tan_n', 'tan_n');
QAST::MASTOperations.add_core_moarop_mapping('atan_n', 'atan_n');
QAST::MASTOperations.add_core_moarop_mapping('atan2_n', 'atan2_n');
QAST::MASTOperations.add_core_moarop_mapping('sec_n', 'sec_n');
QAST::MASTOperations.add_core_moarop_mapping('asec_n', 'asec_n');
QAST::MASTOperations.add_core_moarop_mapping('asin_n', 'asin_n');
QAST::MASTOperations.add_core_moarop_mapping('sinh_n', 'sinh_n');
QAST::MASTOperations.add_core_moarop_mapping('cosh_n', 'cosh_n');
QAST::MASTOperations.add_core_moarop_mapping('tanh_n', 'tanh_n');
QAST::MASTOperations.add_core_moarop_mapping('sech_n', 'sech_n');


QAST::MASTOperations.add_core_moarop_mapping('isnull', 'isnull');
QAST::MASTOperations.add_core_moarop_mapping('null', 'null');
QAST::MASTOperations.add_core_moarop_mapping('can', 'can');
QAST::MASTOperations.add_core_moarop_mapping('time_i', 'time_i');
QAST::MASTOperations.add_core_moarop_mapping('time_n', 'time_n');
QAST::MASTOperations.add_core_moarop_mapping('concat', 'concat_s');

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
        next if ~$_ eq '$allops';
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    $op := $op.name if nqp::istype($op, QAST::Op);
    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);
    
    nqp::push(@dest, MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    ));
}

sub push_ilist(@dest, $src) is export {
    nqp::splice(@dest, $src.instructions, +@dest, 0);
}
