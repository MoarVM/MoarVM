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
            my $operand := @operands[$operand_num++];
            my $operand_kind := ($operand +& $MVM_operand_type_mask);
            my $constant_operand := !($operand +& $MVM_operand_rw_mask);
            my $arg := $operand_kind == $MVM_operand_type_var
                ?? $qastcomp.as_mast($_)
                !! $qastcomp.as_mast($_, :want($operand_kind/8));
            my $arg_kind := $arg.result_kind;

            if $arg_num == 0 && nqp::substr($op, 0, 7) eq 'return_' {
                $*BLOCK.return_kind($arg.result_kind);
            }

            # args cannot be void
            if $arg_kind == $MVM_reg_void {
                nqp::die("Cannot use a void register as an argument to op '$op'");
            }

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
                $qastcomp.coerce($arg, $operand_kind/8);
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

            $arg_num++;
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
        nqp::defined($want)
            ?? $qastcomp.coerce(MAST::InstructionList.new(@all_ins, $result_reg, $result_kind), $want)
            !! MAST::InstructionList.new(@all_ins, $result_reg, $result_kind);
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
        if $type == $MVM_reg_void {
            nqp::die("cannot box a void register");
        }
        (%hll_box{$hll}{$type} // %hll_box{''}{$type})($qastcomp, $reg)
    }

    # Generates instructions to unbox the result in reg.
    method unbox($qastcomp, $hll, $type, $reg) {
        (%hll_unbox{$hll}{$type} // %hll_unbox{''}{$type})($qastcomp, $reg)
    }
}

# Set of sequential statements
QAST::MASTOperations.add_core_op('stmts', -> $qastcomp, $op {
    $qastcomp.as_mast(QAST::Stmts.new( |@($op) ))
});

# Data structures
QAST::MASTOperations.add_core_op('list', -> $qastcomp, $op {
    # Just desugar to create the empty list.
    my $arr := $qastcomp.as_mast(QAST::Op.new(
        :op('create'),
        QAST::Op.new( :op('hlllist') )
    ));
    if +$op.list {
        my $arr_reg := $arr.result_reg;
        # Push things to the list.
        for $op.list {
            my $item := $qastcomp.as_mast($_, :want($MVM_reg_obj));
            my $item_reg := $item.result_reg;
            $arr.append($item);
            push_op($arr.instructions, 'push_o', $arr_reg, $item_reg);
            $*REGALLOC.release_register($item_reg, $MVM_reg_obj);
        }
        my $newer := MAST::InstructionList.new(nqp::list(), $arr_reg, $MVM_reg_obj);
        $arr.append($newer);
    }
    $arr
});
QAST::MASTOperations.add_core_op('list_i', -> $qastcomp, $op {
    # Just desugar to create the empty list.
    my $arr := $qastcomp.as_mast(QAST::Op.new(
        :op('create'),
        QAST::Op.new( :op('bootintarray') )
    ));
    if +$op.list {
        my $arr_reg := $arr.result_reg;
        # Push things to the list.
        for $op.list {
            my $item := $qastcomp.as_mast($_, :want($MVM_reg_int64));
            my $item_reg := $item.result_reg;
            $arr.append($item);
            push_op($arr.instructions, 'push_i', $arr_reg, $item_reg);
            $*REGALLOC.release_register($item_reg, $MVM_reg_int64);
        }
        my $newer := MAST::InstructionList.new(nqp::list(), $arr_reg, $MVM_reg_obj);
        $arr.append($newer);
    }
    $arr
});
QAST::MASTOperations.add_core_op('list_n', -> $qastcomp, $op {
    # Just desugar to create the empty list.
    my $arr := $qastcomp.as_mast(QAST::Op.new(
        :op('create'),
        QAST::Op.new( :op('bootnumarray') )
    ));
    if +$op.list {
        my $arr_reg := $arr.result_reg;
        # Push things to the list.
        for $op.list {
            my $item := $qastcomp.as_mast($_, :want($MVM_reg_num64));
            my $item_reg := $item.result_reg;
            $arr.append($item);
            push_op($arr.instructions, 'push_n', $arr_reg, $item_reg);
            $*REGALLOC.release_register($item_reg, $MVM_reg_num64);
        }
        my $newer := MAST::InstructionList.new(nqp::list(), $arr_reg, $MVM_reg_obj);
        $arr.append($newer);
    }
    $arr
});
QAST::MASTOperations.add_core_op('list_s', -> $qastcomp, $op {
    # Just desugar to create the empty list.
    my $arr := $qastcomp.as_mast(QAST::Op.new(
        :op('create'),
        QAST::Op.new( :op('bootstrarray') )
    ));
    if +$op.list {
        my $arr_reg := $arr.result_reg;
        # Push things to the list.
        for $op.list {
            my $item := $qastcomp.as_mast($_, :want($MVM_reg_str));
            my $item_reg := $item.result_reg;
            $arr.append($item);
            push_op($arr.instructions, 'push_s', $arr_reg, $item_reg);
            $*REGALLOC.release_register($item_reg, $MVM_reg_str);
        }
        my $newer := MAST::InstructionList.new(nqp::list(), $arr_reg, $MVM_reg_obj);
        $arr.append($newer);
    }
    $arr
});
QAST::MASTOperations.add_core_op('list_b', -> $qastcomp, $op {
    # Just desugar to create the empty list.
    my $arr := $qastcomp.as_mast(QAST::Op.new(
        :op('create'),
        QAST::Op.new( :op('bootarray') )
    ));
    if +$op.list {
        my $arr_reg := $arr.result_reg;
        # Push things to the list.
        for $op.list {
            nqp::die("list_b must have a list of blocks")
                unless nqp::istype($_, QAST::Block);
            my $cuid  := $_.cuid();
            my $frame := %*MAST_FRAMES{$cuid};
            my $item_reg := $*REGALLOC.fresh_register($MVM_reg_obj);
            push_op($arr.instructions, 'getcode', $item_reg, $frame);
            push_op($arr.instructions, 'push_o', $arr_reg, $item_reg);
            $*REGALLOC.release_register($item_reg, $MVM_reg_obj);
        }
        my $newer := MAST::InstructionList.new(nqp::list(), $arr_reg, $MVM_reg_obj);
        $arr.append($newer);
    }
    $arr
});
QAST::MASTOperations.add_core_op('qlist', -> $qastcomp, $op {
    $qastcomp.as_mast(QAST::Op.new( :op('list'), |@($op) ))
});
QAST::MASTOperations.add_core_op('hash', -> $qastcomp, $op {
    # Just desugar to create the empty hash.
    my $hash := $qastcomp.as_mast(QAST::Op.new(
        :op('create'),
        QAST::Op.new( :op('hllhash') )
    ));
    if +$op.list {
        my $hash_reg := $hash.result_reg;
        for $op.list -> $key, $val {
            my $key_mast := $qastcomp.as_mast($key, :want($MVM_reg_str));
            my $val_mast := $qastcomp.as_mast($val, :want($MVM_reg_obj));
            my $key_reg := $key_mast.result_reg;
            my $val_reg := $val_mast.result_reg;
            $hash.append($key_mast);
            $hash.append($val_mast);
            push_op($hash.instructions, 'bindkey_o', $hash_reg, $key_reg, $val_reg);
            $*REGALLOC.release_register($key_reg, $MVM_reg_str);
            $*REGALLOC.release_register($val_reg, $MVM_reg_obj);
        }
        my $newer := MAST::InstructionList.new(nqp::list(), $hash_reg, $MVM_reg_obj);
        $hash.append($newer);
    }
    $hash
});

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

        my $res_kind;
        my $res_reg;
        my $is_void := nqp::defined($*WANT) && $*WANT == $MVM_reg_void;
        if $is_void {
            $res_reg := MAST::VOID;
        }
        else {
            $res_kind := $operands == 3
                ?? (@comp_ops[1].result_kind == @comp_ops[2].result_kind
                    ?? @comp_ops[1].result_kind
                    !! $MVM_reg_obj)
                !! (@comp_ops[0].result_kind == @comp_ops[1].result_kind
                    ?? @comp_ops[0].result_kind
                    !! $MVM_reg_obj);
            $res_reg := $*REGALLOC.fresh_register($res_kind);
        }

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
            if !$is_void {
                if @comp_ops[2].result_kind != $res_kind {
                    my $coercion := $qastcomp.coercion(@comp_ops[2], $res_kind);
                    push_ilist(@ins, $coercion);
                    push_op(@ins, 'set', $res_reg, $coercion.result_reg);
                }
                else {
                    push_op(@ins, 'set', $res_reg, @comp_ops[2].result_reg);
                }
            }
            $*REGALLOC.release_register(@comp_ops[2].result_reg, @comp_ops[2].result_kind);
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

QAST::MASTOperations.add_core_op('defor', -> $qastcomp, $op {
    if +$op.list != 2 {
        nqp::die("Operation 'defor' needs 2 operands");
    }

    # Compile the expression.
    my $res_reg := $*REGALLOC.fresh_o();
    my $expr := $qastcomp.as_mast($op[0], :want($MVM_reg_obj));

    # Emit defined check.
    my $def_reg := $*REGALLOC.fresh_i();
    my $lbl := MAST::Label.new($qastcomp.unique('defor'));
    push_op($expr.instructions, 'set', $res_reg, $expr.result_reg);
    push_op($expr.instructions, 'isconcrete', $def_reg, $res_reg);
    push_op($expr.instructions, 'if_i', $def_reg, $lbl);
    $*REGALLOC.release_register($def_reg, $MVM_reg_int64);

    # Emit "then" part.
    my $then := $qastcomp.as_mast($op[1], :want($MVM_reg_obj));
    $*REGALLOC.release_register($expr.result_reg, $MVM_reg_obj);
    $expr.append($then);
    push_op($expr.instructions, 'set', $res_reg, $then.result_reg);
    nqp::push($expr.instructions, $lbl);
    $*REGALLOC.release_register($then.result_reg, $MVM_reg_obj);
    my $newer := MAST::InstructionList.new(nqp::list(), $res_reg, $MVM_reg_obj);
    $expr.append($newer);

    $expr
});

QAST::MASTOperations.add_core_op('ifnull', -> $qastcomp, $op {
    if +$op.list != 2 {
        nqp::die("The 'ifnull' op expects two children");
    }

    # Compile the expression.
    my $res_reg := $*REGALLOC.fresh_o();
    my $expr := $qastcomp.as_mast($op[0], :want($MVM_reg_obj));

    # Emit null check.
    my $lbl := MAST::Label.new($qastcomp.unique('ifnull'));
    push_op($expr.instructions, 'set', $res_reg, $expr.result_reg);
    push_op($expr.instructions, 'ifnonnull', $expr.result_reg, $lbl);

    # Emit "then" part.
    my $then := $qastcomp.as_mast($op[1], :want($MVM_reg_obj));
    $*REGALLOC.release_register($expr.result_reg, $MVM_reg_obj);
    $expr.append($then);
    push_op($expr.instructions, 'set', $res_reg, $then.result_reg);
    nqp::push($expr.instructions, $lbl);
    $*REGALLOC.release_register($then.result_reg, $MVM_reg_obj);
    my $newer := MAST::InstructionList.new(nqp::list(), $res_reg, $MVM_reg_obj);
    $expr.append($newer);

    $expr
});

# Loops.
for ('', 'repeat_') -> $repness {
    for <while until> -> $op_name {
        QAST::MASTOperations.add_core_op("$repness$op_name", -> $qastcomp, $op {
            # Create labels.
            my $while_id := $qastcomp.unique($op_name);
            my $test_lbl := MAST::Label.new($while_id ~ '_test');
            my $next_lbl := MAST::Label.new($while_id ~ '_next');
            my $redo_lbl := MAST::Label.new($while_id ~ '_redo');
            my $hand_lbl := MAST::Label.new($while_id ~ '_handlers');
            my $done_lbl := MAST::Label.new($while_id ~ '_done');

            # Compile each of the children; we'll need to look at the result
            # types and pick an overall result type if in non-void context.
            my @comp_ops;
            my @comp_types;
            my $handler := 1;
            my $*IMM_ARG;
            for $op.list {
                if $_.named eq 'nohandler' { $handler := 0; }
                else {
                    my $*HAVE_IMM_ARG := $_.arity > 0 && $_ =:= $op.list[1];
                    my $comp := $qastcomp.as_mast($_);
                    @comp_ops.push($comp);
                    @comp_types.push($comp.result_kind);
                    if $*HAVE_IMM_ARG && !$*IMM_ARG {
                        nqp::die("$op_name block expects an argument, but there's no immediate block to take it");
                    }
                }
            }
            my $res_kind := @comp_types[0] == @comp_types[1]
                ?? @comp_types[0]
                !! $MVM_reg_obj;
            my $res_reg := $*REGALLOC.fresh_register($res_kind);

            # Check operand count.
            my $operands := +@comp_ops;
            nqp::die("Operation '$repness$op_name' needs 2 or 3 operands")
                if $operands != 2 && $operands != 3;

            # Test the condition and jump to the loop end if it's
            # not met.
            my @loop_il;
            my $coerced := $qastcomp.coerce(@comp_ops[0], $res_kind);
            if $repness {
                # It's a repeat_ variant, need to go straight into the
                # loop body unconditionally. Be sure to set the register
                # for the result to something first.
                if $res_kind == $MVM_reg_obj {
                    push_op(@loop_il, 'null', $res_reg);
                }
                elsif $res_kind == $MVM_reg_str {
                    push_op(@loop_il, 'null_s', $res_reg);
                }
                elsif $res_kind == $MVM_reg_num64 {
                    push_op(@loop_il, 'const_n64', $res_reg,
                        MAST::NVal.new( :value(0.0) ));
                }
                else {
                    push_op(@loop_il, 'const_i64', $res_reg,
                        MAST::IVal.new( :value(0) ));
                }
                push_op(@loop_il, 'goto', $redo_lbl);
            }
            nqp::push(@loop_il, $test_lbl);
            push_ilist(@loop_il, $coerced);
            push_op(@loop_il, 'set', $res_reg, $coerced.result_reg);
            push_op(@loop_il,
                resolve_condition_op(@comp_ops[0].result_kind, $op_name eq 'while'),
                @comp_ops[0].result_reg,
                $done_lbl
            );

            # Handle immediate blocks wanting the value as an arg.
            if $*IMM_ARG {
                $*IMM_ARG($res_reg);
            }

            # Emit the loop body; stash the result.
            my $body := $qastcomp.coerce(@comp_ops[1], $res_kind);
            nqp::push(@loop_il, $redo_lbl);
            push_ilist(@loop_il, $body);
            push_op(@loop_il, 'set', $res_reg, $body.result_reg);

            # If there's a third child, evaluate it as part of the
            # "next".
            if $operands == 3 {
                nqp::push(@loop_il, $next_lbl);
                push_ilist(@loop_il, @comp_ops[2]);
            }

            # Emit the iteration jump.
            push_op(@loop_il, 'goto', $test_lbl);

            # Emit postlude, with exception handlers if needed. Note that we
            # don't actually need to emit a bunch of handlers; since a handler
            # scope will happily throw control to a label of our choosing, we
            # just have the goto label be the place the control exception
            # needs to send control to.
            if $handler {
                my @redo_il := [MAST::HandlerScope.new(
                    :instructions(@loop_il),
                    :category_mask($HandlerCategory::redo),
                    :action($HandlerAction::unwind_and_goto),
                    :goto($redo_lbl)
                )];
                my @next_il := [MAST::HandlerScope.new(
                    :instructions(@redo_il),
                    :category_mask($HandlerCategory::next),
                    :action($HandlerAction::unwind_and_goto),
                    :goto($operands == 3 ?? $next_lbl !! $test_lbl)
                )];
                my @last_il := [MAST::HandlerScope.new(
                    :instructions(@next_il),
                    :category_mask($HandlerCategory::last),
                    :action($HandlerAction::unwind_and_goto),
                    :goto($done_lbl)
                )];
                nqp::push(@last_il, $done_lbl);
                MAST::InstructionList.new(@last_il, $res_reg, $res_kind)
            }
            else {
                nqp::push(@loop_il, $done_lbl);
                MAST::InstructionList.new(@loop_il, $res_reg, $res_kind)
            }
        });
    }
}

QAST::MASTOperations.add_core_op('for', -> $qastcomp, $op {
    my $handler := 1;
    my @operands;
    for $op.list {
        if $_.named eq 'nohandler' { $handler := 0; }
        else { @operands.push($_) }
    }

    if +@operands != 2 {
        nqp::die("Operation 'for' needs 2 operands");
    }
    unless nqp::istype(@operands[1], QAST::Block) {
        nqp::die("Operation 'for' expects a block as its second operand");
    }
    if @operands[1].blocktype eq 'immediate' {
        @operands[1].blocktype('declaration');
    }

    # Create result temporary if we'll need one.
    my $res := $*WANT == $MVM_reg_void ?? 0 !! $*REGALLOC.fresh_o();

    # Evaluate the thing we'll iterate over, get the iterator and
    # store it in a temporary.
    my $il := [];
    my $list_il := $qastcomp.as_mast(@operands[0], :want($MVM_reg_obj));
    push_ilist($il, $list_il);
    if $res {
        push_op($il, 'set', $res, $list_il.result_reg);
    }
    my $iter_tmp := $*REGALLOC.fresh_o();
    push_op($il, 'iter', $iter_tmp, $list_il.result_reg);

    # Do similar for the block.
    my $block_res := $qastcomp.as_mast(@operands[1], :want($MVM_reg_obj));
    push_ilist($il, $block_res);

    # Some labels we'll need.
    my $for_id := $qastcomp.unique('for');
    my $lbl_next := MAST::Label.new( :name($for_id ~ 'next') );
    my $lbl_redo := MAST::Label.new( :name($for_id ~ 'redo') );
    my $lbl_done := MAST::Label.new( :name($for_id ~ 'done') );

    # Emit loop test.
    my $loop_il := ();
    nqp::push($loop_il, $lbl_next);
    push_op($loop_il, 'unless_o', $iter_tmp, $lbl_done);
    $loop_il := MAST::InstructionList.new($loop_il, MAST::VOID, $MVM_reg_void);

    # Fetch values into temporaries (on the stack ain't enough in case
    # of redo).
    my $val_il := ();
    my @val_temps;
    my @arg_flags;
    my $arity := @operands[1].arity || 1;
    while $arity > 0 {
        my $tmp := $*REGALLOC.fresh_o();
        push_op($val_il, 'shift_o', $tmp, $iter_tmp);
        nqp::push(@val_temps, $tmp);
        nqp::push(@arg_flags, $Arg::obj);
        $arity := $arity - 1;
    }
    nqp::push($val_il, $lbl_redo);
    $val_il := MAST::InstructionList.new($val_il, MAST::VOID, $MVM_reg_void);

    # Now do block invocation.

    my $inv_il := $res
        ?? MAST::Call.new(
            :target($block_res.result_reg),
            :flags(@arg_flags),
            |@val_temps,
            :result($res)
        )
        !! MAST::Call.new(
            :target($block_res.result_reg),
            :flags(@arg_flags),
            |@val_temps
        );
    $inv_il := MAST::InstructionList.new([$inv_il], MAST::VOID, $MVM_reg_void);

#    # Wrap block invocation in redo handler if needed.
#    if $handler {
#        my $catch := JAST::InstructionList.new();
#        $catch.append(JAST::Instruction.new( :op('pop') ));
#        $catch.append(JAST::Instruction.new( :op('goto'), $lbl_redo ));
#        $inv_il := JAST::TryCatch.new( :try($inv_il), :$catch, :type($TYPE_EX_REDO) );
#    }
    push_ilist($val_il.instructions, $inv_il);

#    # Wrap value fetching and call in "next" handler if needed.
#    if $handler {
#        $val_il := JAST::TryCatch.new(
#            :try($val_il),
#            :catch(JAST::Instruction.new( :op('pop') )),
#            :type($TYPE_EX_NEXT)
#        );
#    }
    push_ilist($loop_il.instructions, $val_il);
    push_op($loop_il.instructions, 'goto', $lbl_next );

#    # Emit postlude, wrapping in last handler if needed.
#    if $handler {
#        my $catch := JAST::InstructionList.new();
#        $catch.append(JAST::Instruction.new( :op('pop') ));
#        $catch.append(JAST::Instruction.new( :op('goto'), $lbl_done ));
#        $loop_il := JAST::TryCatch.new( :try($loop_il), :$catch, :type($TYPE_EX_LAST) );
#    }
    push_ilist($il, $loop_il);
    nqp::push($il, $lbl_done);

    # Result, as needed.
    my $result := $res ?? MAST::InstructionList.new($il, $res, $*WANT) !! MAST::InstructionList.new($il, MAST::VOID, $MVM_reg_void);
    $*REGALLOC.release_register($list_il.result_reg, $list_il.result_kind);
    $*REGALLOC.release_register($block_res.result_reg, $block_res.result_kind);
    for @val_temps { $*REGALLOC.release_register($_, $MVM_reg_obj) }
    $*REGALLOC.release_register($inv_il.result_reg, $inv_il.result_kind);
    $result
});

# Calling
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
    if nqp::can($arg, 'flat') && $arg.flat {
        if $arg.named {
            $result_typeflag := $result_typeflag +| $Arg::flatnamed;
        }
        else {
            $result_typeflag := $result_typeflag +| $Arg::flat;
        }
    }
    elsif nqp::can($arg, 'named') && $arg.named -> $name {
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
        nqp::push(((nqp::can($_, 'named') && $_.named && (!nqp::can($_, 'flat') || !$_.flat)) ?? @named !! @posit), $_);
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
            $arg_num++;
        }
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
    my @args := nqp::clone($op.list);
    if +@args == 0 {
        nqp::die('Method call node requires at least one child');
    }
    my $invocant := $qastcomp.as_mast(@args.shift(), :want($MVM_reg_obj));
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
        my $method_name_ilist := $qastcomp.as_mast($methodname_expr, :want($MVM_reg_str));
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
            $arg_num++;
        }
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

QAST::MASTOperations.add_core_op('lexotic', -> $qastcomp, $op {
    my $lex_label := MAST::Label.new( :name($qastcomp.unique('lexotic_')) );
    my $end_label := MAST::Label.new( :name($qastcomp.unique('lexotic_end_')) );

    # Create new lexotic and install it lexically.
    my @ins;
    my $lex_tmp := $*REGALLOC.fresh_register($MVM_reg_obj);
    $*BLOCK.add_lexical(QAST::Var.new( :name($op.name), :scope('lexical'), :decl('var') ));
    push_op(@ins, 'newlexotic', $lex_tmp, $lex_label);
    push_op(@ins, 'bindlex', $*BLOCK.lexical($op.name), $lex_tmp);

    # Emit the body, and go to the end of the lexotic code; the body's result
    # is what we want.
    my $body := $qastcomp.compile_all_the_stmts($op.list, :want($MVM_reg_obj));
    nqp::push(@ins, MAST::HandlerScope.new(
        :instructions($body.instructions),
        :category_mask($HandlerCategory::return),
        :action($HandlerAction::unwind_and_goto),
        :goto($lex_label)
    ));
    push_op(@ins, 'goto', $end_label);

    # Finally, emit the lexotic handler.
    nqp::push(@ins, $lex_label);
    push_op(@ins, 'lexoticresult', $body.result_reg, $lex_tmp);
    nqp::push(@ins, $end_label);

    $*REGALLOC.release_register($lex_tmp, $MVM_reg_obj);

    MAST::InstructionList.new(@ins, $body.result_reg, $MVM_reg_obj)
});

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

# Exception handling/munging.
QAST::MASTOperations.add_core_moarop_mapping('die', 'die');
QAST::MASTOperations.add_core_moarop_mapping('die_s', 'die');
QAST::MASTOperations.add_core_moarop_mapping('exception', 'exception');
QAST::MASTOperations.add_core_moarop_mapping('getextype', 'getexcategory');
QAST::MASTOperations.add_core_moarop_mapping('setextype', 'bindexcategory', 1);
QAST::MASTOperations.add_core_moarop_mapping('getpayload', 'getexpayload');
QAST::MASTOperations.add_core_moarop_mapping('setpayload', 'bindexpayload', 1);
QAST::MASTOperations.add_core_moarop_mapping('getmessage', 'getexmessage');
QAST::MASTOperations.add_core_moarop_mapping('setmessage', 'bindexmessage', 1);
QAST::MASTOperations.add_core_moarop_mapping('newexception', 'newexception');
QAST::MASTOperations.add_core_moarop_mapping('backtracestrings', 'backtracestrings');
# XXX backtrace
QAST::MASTOperations.add_core_moarop_mapping('throw', 'throwdyn');
# XXX rethrow, resume

my %handler_names := nqp::hash(
    'CATCH',   $HandlerCategory::catch,
    'CONTROL', $HandlerCategory::control,
    'NEXT',    $HandlerCategory::next,
    'LAST',    $HandlerCategory::last,
    'REDO',    $HandlerCategory::redo,
    'TAKE',    $HandlerCategory::take,
    'WARN',    $HandlerCategory::warn,
    'PROCEED', $HandlerCategory::proceed,
    'SUCCEED', $HandlerCategory::succeed,
);
QAST::MASTOperations.add_core_op('handle', -> $qastcomp, $op {
    my @children := nqp::clone($op.list());
    if @children == 0 {
        nqp::die("The 'handle' op requires at least one child");
    }

    # If there's exactly one child, then there's nothing protecting
    # it; just compile it and we're done.
    my $protected := @children.shift();
    unless @children {
        return $qastcomp.as_mast($protected);
    }

    # Otherwise, we need to generate and install a handler block, which will
    # decide that to do by category.
    my $mask := 0;
    my $hblock := QAST::Block.new(
        QAST::Op.new(
            :op('bind'),
            QAST::Var.new( :name('__category__'), :scope('local'), :decl('var') ),
            QAST::Op.new(
                :op('getextype'),
                QAST::Op.new( :op('exception') )
            )));
    my $push_target := $hblock;
    for @children -> $type, $handler {
        # Get the category mask.
        unless nqp::existskey(%handler_names, $type) {
            nqp::die("Invalid handler type '$type'");
        }
        my $cat_mask := %handler_names{$type};

        # Chain in this handler.
        my $check := QAST::Op.new(
            :op('if'),
            QAST::Op.new(
                :op('bitand_i'),
                QAST::Var.new( :name('__category__'), :scope('local') ),
                QAST::IVal.new( :value($cat_mask) )
            ),
            $handler
        );
        $push_target.push($check);
        $push_target := $check;

        # Add to mask.
        $mask := nqp::bitor_i($mask, $cat_mask);
    }

    # Add a local and store the handler block into it.
    my $hblocal := MAST::Local.new($*MAST_FRAME.add_local(NQPMu));
    my $il      := nqp::list();
    my $hbmast  := $qastcomp.as_mast($hblock, :want($MVM_reg_obj));
    push_ilist($il, $hbmast);
    push_op($il, 'set', $hblocal, $hbmast.result_reg);
    $*REGALLOC.release_register($hbmast.result_reg, $MVM_reg_obj);

    # Wrap instructions to try up in a handler.
    my $protil := $qastcomp.as_mast($protected, :want($MVM_reg_obj));
    my $endlbl := MAST::Label.new( :name($qastcomp.unique('handle_end_')) );
    nqp::push($il, MAST::HandlerScope.new(
        :instructions($protil.instructions), :goto($endlbl), :block($hblocal),
        :category_mask($mask), :action($HandlerAction::invoke_and_we'll_see)));
    nqp::push($il, $endlbl);

    # XXX Result not quite right here yet.
    MAST::InstructionList.new($il, $protil.result_reg, $MVM_reg_obj)
});

# Control exception throwing.
my %control_map := nqp::hash(
    'next', $HandlerCategory::next,
    'last', $HandlerCategory::last,
    'redo', $HandlerCategory::redo
);
QAST::MASTOperations.add_core_op('control', -> $qastcomp, $op {
    my $name := $op.name;
    if nqp::existskey(%control_map, $name) {
        my $il := nqp::list();
        my $res := $*REGALLOC.fresh_register($MVM_reg_obj);
        push_op($il, 'throwcatdyn', $res,
            MAST::IVal.new( :value(%control_map{$name}) ));
        MAST::InstructionList.new($il, $res, $MVM_reg_obj)
    }
    else {
        nqp::die("Unknown control exception type '$name'");
    }
});

# Default ways to box/unbox (for no particular HLL).
QAST::MASTOperations.add_hll_unbox('', $MVM_reg_int64, -> $qastcomp, $reg {
    my $il := nqp::list();
    my $a := $*REGALLOC.fresh_register($MVM_reg_num64);
    my $b := $*REGALLOC.fresh_register($MVM_reg_int64);
    push_op($il, 'smrt_numify', $a, $reg);
    push_op($il, 'coerce_ni', $b, $a);
    $*REGALLOC.release_register($a, $MVM_reg_num64);
    $*REGALLOC.release_register($reg, $MVM_reg_obj);
    MAST::InstructionList.new($il, $b, $MVM_reg_int64)
});
QAST::MASTOperations.add_hll_unbox('', $MVM_reg_num64, -> $qastcomp, $reg {
    my $il := nqp::list();
    my $res_reg := $*REGALLOC.fresh_register($MVM_reg_num64);
    push_op($il, 'smrt_numify', $res_reg, $reg);
    $*REGALLOC.release_register($reg, $MVM_reg_obj);
    MAST::InstructionList.new($il, $res_reg, $MVM_reg_num64)
});
QAST::MASTOperations.add_hll_unbox('', $MVM_reg_str, -> $qastcomp, $reg {
    my $il := nqp::list();
    my $res_reg := $*REGALLOC.fresh_register($MVM_reg_str);
    push_op($il, 'smrt_strify', $res_reg, $reg);
    $*REGALLOC.release_register($reg, $MVM_reg_obj);
    MAST::InstructionList.new($il, $res_reg, $MVM_reg_str)
});
sub boxer($kind, $type_op, $op) {
    -> $qastcomp, $reg {
        my $il := nqp::list();
        my $res_reg := $*REGALLOC.fresh_register($MVM_reg_obj);
        push_op($il, $type_op, $res_reg);
        push_op($il, $op, $res_reg, $reg, $res_reg);
        $*REGALLOC.release_register($reg, $kind);
        MAST::InstructionList.new($il, $res_reg, $MVM_reg_obj)
    }
}
QAST::MASTOperations.add_hll_box('', $MVM_reg_int64, boxer($MVM_reg_int64, 'hllboxtype_i', 'box_i'));
QAST::MASTOperations.add_hll_box('', $MVM_reg_num64, boxer($MVM_reg_num64, 'hllboxtype_n', 'box_n'));
QAST::MASTOperations.add_hll_box('', $MVM_reg_str, boxer($MVM_reg_str, 'hllboxtype_s', 'box_s'));

# Context introspection
QAST::MASTOperations.add_core_moarop_mapping('ctx', 'ctx');
QAST::MASTOperations.add_core_moarop_mapping('ctxouter', 'ctxouter');
QAST::MASTOperations.add_core_moarop_mapping('ctxcaller', 'ctxcaller');
QAST::MASTOperations.add_core_moarop_mapping('curcode', 'curcode');
QAST::MASTOperations.add_core_moarop_mapping('callercode', 'callercode');
QAST::MASTOperations.add_core_moarop_mapping('ctxlexpad', 'ctxlexpad');
QAST::MASTOperations.add_core_moarop_mapping('curlexpad', 'ctx');
QAST::MASTOperations.add_core_moarop_mapping('lexprimspec', 'lexprimspec');

# Argument capture processing, for writing things like multi-dispatchers in
# high level languages.
QAST::MASTOperations.add_core_moarop_mapping('usecapture', 'usecapture');
QAST::MASTOperations.add_core_moarop_mapping('savecapture', 'savecapture');
QAST::MASTOperations.add_core_moarop_mapping('captureposelems', 'captureposelems');
QAST::MASTOperations.add_core_moarop_mapping('captureposarg', 'captureposarg');
QAST::MASTOperations.add_core_moarop_mapping('captureposarg_i', 'captureposarg_i');
QAST::MASTOperations.add_core_moarop_mapping('captureposarg_n', 'captureposarg_n');
QAST::MASTOperations.add_core_moarop_mapping('captureposarg_s', 'captureposarg_s');
QAST::MASTOperations.add_core_moarop_mapping('captureposprimspec', 'captureposprimspec');
QAST::MASTOperations.add_core_moarop_mapping('objprimspec', 'objprimspec');

# Multiple dispatch related.
QAST::MASTOperations.add_core_moarop_mapping('invokewithcapture', 'invokewithcapture');
QAST::MASTOperations.add_core_moarop_mapping('multicacheadd', 'multicacheadd');
QAST::MASTOperations.add_core_moarop_mapping('multicachefind', 'multicachefind');

# Constant mapping.
my %const_map := nqp::hash(
    'CCLASS_ANY',           65535,
    'CCLASS_UPPERCASE',     1,
    'CCLASS_LOWERCASE',     2,
    'CCLASS_ALPHABETIC',    4,
    'CCLASS_NUMERIC',       8,
    'CCLASS_HEXADECIMAL',   16,
    'CCLASS_WHITESPACE',    32,
    'CCLASS_BLANK',         256,
    'CCLASS_CONTROL',       512,
    'CCLASS_PUNCTUATION',   1024,
    'CCLASS_ALPHANUMERIC',  2048,
    'CCLASS_NEWLINE',       4096,
    'CCLASS_WORD',          8192
);
QAST::MASTOperations.add_core_op('const', -> $qastcomp, $op {
    if nqp::existskey(%const_map, $op.name) {
        $qastcomp.as_mast(QAST::IVal.new( :value(%const_map{$op.name}) ))
    }
    else {
        nqp::die("Unknown constant '" ~ $op.name ~ "'");
    }
});

# Default way to do positional and associative lookups.
QAST::MASTOperations.add_core_moarop_mapping('positional_get', 'atpos_o');
QAST::MASTOperations.add_core_moarop_mapping('positional_bind', 'bindpos_o', 2);
QAST::MASTOperations.add_core_moarop_mapping('associative_get', 'atkey_o');
QAST::MASTOperations.add_core_moarop_mapping('associative_bind', 'bindkey_o', 2);

# I/O opcodes
QAST::MASTOperations.add_core_moarop_mapping('say', 'say', 0);
QAST::MASTOperations.add_core_moarop_mapping('print', 'print', 0);
QAST::MASTOperations.add_core_moarop_mapping('stat', 'stat');
QAST::MASTOperations.add_core_moarop_mapping('open', 'open_fh');
QAST::MASTOperations.add_core_moarop_mapping('getstdin', 'getstdin');
QAST::MASTOperations.add_core_moarop_mapping('getstdout', 'getstdout');
QAST::MASTOperations.add_core_moarop_mapping('getstderr', 'getstderr');
QAST::MASTOperations.add_core_moarop_mapping('setencoding', 'setencoding');
QAST::MASTOperations.add_core_moarop_mapping('tellfh', 'tell_fh');
QAST::MASTOperations.add_core_moarop_mapping('printfh', 'write_fhs');
# QAST::MASTOperations.add_core_moarop_mapping('sayfh', ?);
QAST::MASTOperations.add_core_moarop_mapping('readlinefh', 'readline_fh');
# QAST::MASTOperations.add_core_moarop_mapping('readlineintfh', ?);
QAST::MASTOperations.add_core_moarop_mapping('readallfh', 'readall_fh');
QAST::MASTOperations.add_core_moarop_mapping('eoffh', 'eof_fh');
QAST::MASTOperations.add_core_moarop_mapping('closefh', 'close_fh', 0);

QAST::MASTOperations.add_core_moarop_mapping('chmod', 'chmod_f', 0);
QAST::MASTOperations.add_core_moarop_mapping('unlink', 'delete_f', 0);
QAST::MASTOperations.add_core_moarop_mapping('rmdir', 'rmdir', 0);
# QAST::MASTOperations.add_core_moarop_mapping('cwd', ?);
QAST::MASTOperations.add_core_moarop_mapping('chdir', 'chdir', 0);
QAST::MASTOperations.add_core_moarop_mapping('mkdir', 'mkdir', 0);
QAST::MASTOperations.add_core_moarop_mapping('rename', 'rename_f', 0);
QAST::MASTOperations.add_core_moarop_mapping('copy', 'copy_f', 0);
# QAST::MASTOperations.add_core_moarop_mapping('symlink', ?);
# QAST::MASTOperations.add_core_moarop_mapping('link', ?);
QAST::MASTOperations.add_core_op('sprintf', -> $qastcomp, $op {
    my @operands := $op.list;
    $qastcomp.as_mast(
        QAST::Op.new(
            :op('call'),
            :returns(str),
            QAST::Op.new(
                :op('gethllsym'),
                QAST::SVal.new( :value('nqp') ),
                QAST::SVal.new( :value('sprintf') )
            ),
            |@operands )
    );
});
QAST::MASTOperations.add_core_op('sprintfaddargumenthandler', -> $qastcomp, $op {
    my @operands := $op.list;
    $qastcomp.as_mast(
        QAST::Op.new(
            :op('call'),
            :returns(str),
            QAST::Op.new(
                :op('gethllsym'),
                QAST::SVal.new( :value('nqp') ),
                QAST::SVal.new( :value('sprintfaddargumenthandler') )
            ),
            |@operands )
    );
});

# terms
QAST::MASTOperations.add_core_moarop_mapping('time_i', 'time_i');
QAST::MASTOperations.add_core_moarop_mapping('time_n', 'time_n');

# Arithmetic ops
QAST::MASTOperations.add_core_moarop_mapping('add_i', 'add_i');
QAST::MASTOperations.add_core_moarop_mapping('add_I', 'add_I');
QAST::MASTOperations.add_core_moarop_mapping('add_n', 'add_n');
QAST::MASTOperations.add_core_moarop_mapping('sub_i', 'sub_i');
QAST::MASTOperations.add_core_moarop_mapping('sub_I', 'sub_I');
QAST::MASTOperations.add_core_moarop_mapping('sub_n', 'sub_n');
QAST::MASTOperations.add_core_moarop_mapping('mul_i', 'mul_i');
QAST::MASTOperations.add_core_moarop_mapping('mul_I', 'mul_I');
QAST::MASTOperations.add_core_moarop_mapping('mul_n', 'mul_n');
QAST::MASTOperations.add_core_moarop_mapping('div_i', 'div_i');
QAST::MASTOperations.add_core_moarop_mapping('div_I', 'div_I');
QAST::MASTOperations.add_core_moarop_mapping('div_In', 'div_In');
QAST::MASTOperations.add_core_moarop_mapping('div_n', 'div_n');
QAST::MASTOperations.add_core_moarop_mapping('mod_i', 'mod_i');
QAST::MASTOperations.add_core_moarop_mapping('mod_I', 'mod_I');
QAST::MASTOperations.add_core_moarop_mapping('expmod_I', 'expmod_I');
QAST::MASTOperations.add_core_moarop_mapping('mod_n', 'mod_n');
QAST::MASTOperations.add_core_moarop_mapping('neg_i', 'neg_i');
QAST::MASTOperations.add_core_moarop_mapping('neg_I', 'neg_I');
QAST::MASTOperations.add_core_moarop_mapping('neg_n', 'neg_n');
QAST::MASTOperations.add_core_moarop_mapping('pow_n', 'pow_n');
QAST::MASTOperations.add_core_moarop_mapping('pow_I', 'pow_I');
QAST::MASTOperations.add_core_moarop_mapping('abs_i', 'abs_i');
QAST::MASTOperations.add_core_moarop_mapping('abs_I', 'abs_I');
QAST::MASTOperations.add_core_moarop_mapping('abs_n', 'abs_n');
QAST::MASTOperations.add_core_moarop_mapping('ceil_n', 'ceil_n');
QAST::MASTOperations.add_core_moarop_mapping('floor_n', 'floor_n');
QAST::MASTOperations.add_core_moarop_mapping('ln_n', 'log_n'); # looks like this one is never used
QAST::MASTOperations.add_core_moarop_mapping('sqrt_n', 'sqrt_n');
QAST::MASTOperations.add_core_moarop_mapping('base_I', 'base_I');
QAST::MASTOperations.add_core_moarop_mapping('radix', 'radix');
QAST::MASTOperations.add_core_moarop_mapping('radix_I', 'radix_I');
QAST::MASTOperations.add_core_moarop_mapping('log_n', 'log_n');
QAST::MASTOperations.add_core_moarop_mapping('exp_n', 'exp_n');
#QAST::MASTOperations.add_core_moarop_mapping('isnanorinf', 'is_inf_or_nan', 'In');
QAST::MASTOperations.add_core_moarop_mapping('isprime_I', 'isprime_I');
QAST::MASTOperations.add_core_moarop_mapping('rand_I', 'rand_I');

# bigint <-> string/num conversions
QAST::MASTOperations.add_core_moarop_mapping('tostr_I', 'coerce_Is');
QAST::MASTOperations.add_core_moarop_mapping('fromstr_I', 'coerce_sI');
QAST::MASTOperations.add_core_moarop_mapping('tonum_I', 'coerce_In');
QAST::MASTOperations.add_core_moarop_mapping('fromnum_I', 'coerce_nI');

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

# esoteric math opcodes
QAST::MASTOperations.add_core_moarop_mapping('gcd_i', 'gcd_i');
QAST::MASTOperations.add_core_moarop_mapping('gcd_I', 'gcd_I');
QAST::MASTOperations.add_core_moarop_mapping('lcm_i', 'lcm_i');
QAST::MASTOperations.add_core_moarop_mapping('lcm_I', 'lcm_I');

# string opcodes
QAST::MASTOperations.add_core_moarop_mapping('chars', 'chars');
QAST::MASTOperations.add_core_moarop_mapping('uc', 'uc');
QAST::MASTOperations.add_core_moarop_mapping('lc', 'lc');
QAST::MASTOperations.add_core_moarop_mapping('tc', 'tc');
QAST::MASTOperations.add_core_moarop_mapping('x', 'repeat_s');
QAST::MASTOperations.add_core_moarop_mapping('iscclass', 'iscclass');
QAST::MASTOperations.add_core_moarop_mapping('findcclass', 'findcclass');
QAST::MASTOperations.add_core_moarop_mapping('findnotcclass', 'findnotcclass');
QAST::MASTOperations.add_core_moarop_mapping('escape', 'escape');
QAST::MASTOperations.add_core_moarop_mapping('flip', 'flip');
QAST::MASTOperations.add_core_moarop_mapping('concat', 'concat_s');
QAST::MASTOperations.add_core_moarop_mapping('join', 'join');
QAST::MASTOperations.add_core_moarop_mapping('split', 'split');
QAST::MASTOperations.add_core_moarop_mapping('chr', 'chr');
QAST::MASTOperations.add_core_moarop_mapping('ordfirst', 'ordfirst');
QAST::MASTOperations.add_core_moarop_mapping('ordat', 'ordat');
QAST::MASTOperations.add_core_moarop_mapping('index_s', 'index_s');
QAST::MASTOperations.add_core_moarop_mapping('rindexfrom', 'rindexfrom');
QAST::MASTOperations.add_core_moarop_mapping('substr_s', 'substr_s');
QAST::MASTOperations.add_core_moarop_mapping('codepointfromname', 'getcpbyname');

QAST::MASTOperations.add_core_op('substr', -> $qastcomp, $op {
    my @operands := $op.list;
    if +@operands == 2 { nqp::push(@operands, QAST::IVal.new( :value(-1) )) }
    $qastcomp.as_mast(QAST::Op.new( :op('substr_s'), |@operands ));
});

QAST::MASTOperations.add_core_op('ord',  -> $qastcomp, $op {
    my @operands := $op.list;
    $qastcomp.as_mast(+@operands == 1
        ?? QAST::Op.new( :op('ordfirst'), |@operands )
        !! QAST::Op.new( :op('ordat'), |@operands ));
});

QAST::MASTOperations.add_core_op('index',  -> $qastcomp, $op {
    my @operands := $op.list;
    $qastcomp.as_mast(+@operands == 2
        ?? QAST::Op.new( :op('index_s'), |@operands, QAST::IVal.new( :value(0)) )
        !! QAST::Op.new( :op('index_s'), |@operands ));
});

QAST::MASTOperations.add_core_op('rindex',  -> $qastcomp, $op {
    my @operands := $op.list;
    $qastcomp.as_mast(+@operands == 2
        ?? QAST::Op.new( :op('rindexfrom'), |@operands, QAST::IVal.new( :value(-1) ) )
        !! QAST::Op.new( :op('rindexfrom'), |@operands ));
});

# serialization context opcodes
QAST::MASTOperations.add_core_moarop_mapping('sha1', 'sha1');
QAST::MASTOperations.add_core_moarop_mapping('createsc', 'createsc');
QAST::MASTOperations.add_core_moarop_mapping('scsetobj', 'scsetobj');
QAST::MASTOperations.add_core_moarop_mapping('scsetcode', 'scsetcode');
QAST::MASTOperations.add_core_moarop_mapping('scgetobj', 'scgetobj');
QAST::MASTOperations.add_core_moarop_mapping('scgethandle', 'scgethandle');
QAST::MASTOperations.add_core_moarop_mapping('scgetobjidx', 'scgetobjidx');
QAST::MASTOperations.add_core_moarop_mapping('scsetdesc', 'scsetdesc');
QAST::MASTOperations.add_core_moarop_mapping('scobjcount', 'scobjcount');
QAST::MASTOperations.add_core_moarop_mapping('setobjsc', 'setobjsc');
QAST::MASTOperations.add_core_moarop_mapping('getobjsc', 'getobjsc');
QAST::MASTOperations.add_core_moarop_mapping('serialize', 'serialize');
QAST::MASTOperations.add_core_moarop_mapping('deserialize', 'deserialize');
QAST::MASTOperations.add_core_moarop_mapping('scwbdisable', 'scwbdisable');
QAST::MASTOperations.add_core_moarop_mapping('scwbenable', 'scwbenable');
QAST::MASTOperations.add_core_moarop_mapping('pushcompsc', 'pushcompsc', 0);
QAST::MASTOperations.add_core_moarop_mapping('popcompsc', 'popcompsc');

# bitwise opcodes
QAST::MASTOperations.add_core_moarop_mapping('bitor_i', 'bor_i');
QAST::MASTOperations.add_core_moarop_mapping('bitxor_i', 'bxor_i');
QAST::MASTOperations.add_core_moarop_mapping('bitand_i', 'band_i');
QAST::MASTOperations.add_core_moarop_mapping('bitshiftl_i', 'blshift_i');
QAST::MASTOperations.add_core_moarop_mapping('bitshiftr_i', 'brshift_i');
QAST::MASTOperations.add_core_moarop_mapping('bitneg_i', 'bnot_i');

QAST::MASTOperations.add_core_moarop_mapping('bitor_I', 'bor_I');
QAST::MASTOperations.add_core_moarop_mapping('bitxor_I', 'bxor_I');
QAST::MASTOperations.add_core_moarop_mapping('bitand_I', 'band_I');
QAST::MASTOperations.add_core_moarop_mapping('bitneg_I', 'bnot_I');
QAST::MASTOperations.add_core_moarop_mapping('bitshiftl_I', 'blshift_I');
QAST::MASTOperations.add_core_moarop_mapping('bitshiftr_I', 'brshift_I');

# relational opcodes
QAST::MASTOperations.add_core_moarop_mapping('cmp_i', 'cmp_i');
QAST::MASTOperations.add_core_moarop_mapping('iseq_i', 'eq_i');
QAST::MASTOperations.add_core_moarop_mapping('isne_i', 'ne_i');
QAST::MASTOperations.add_core_moarop_mapping('islt_i', 'lt_i');
QAST::MASTOperations.add_core_moarop_mapping('isle_i', 'le_i');
QAST::MASTOperations.add_core_moarop_mapping('isgt_i', 'gt_i');
QAST::MASTOperations.add_core_moarop_mapping('isge_i', 'ge_i');

QAST::MASTOperations.add_core_moarop_mapping('cmp_n', 'cmp_n');
QAST::MASTOperations.add_core_moarop_mapping('not_i', 'not_i');
QAST::MASTOperations.add_core_moarop_mapping('iseq_n', 'eq_n');
QAST::MASTOperations.add_core_moarop_mapping('isne_n', 'ne_n');
QAST::MASTOperations.add_core_moarop_mapping('islt_n', 'lt_n');
QAST::MASTOperations.add_core_moarop_mapping('isle_n', 'le_n');
QAST::MASTOperations.add_core_moarop_mapping('isgt_n', 'gt_n');
QAST::MASTOperations.add_core_moarop_mapping('isge_n', 'ge_n');

QAST::MASTOperations.add_core_moarop_mapping('cmp_s', 'cmp_s');
QAST::MASTOperations.add_core_moarop_mapping('iseq_s', 'eq_s');
QAST::MASTOperations.add_core_moarop_mapping('isne_s', 'ne_s');
QAST::MASTOperations.add_core_moarop_mapping('islt_s', 'lt_s');
QAST::MASTOperations.add_core_moarop_mapping('isle_s', 'le_s');
QAST::MASTOperations.add_core_moarop_mapping('isgt_s', 'gt_s');
QAST::MASTOperations.add_core_moarop_mapping('isge_s', 'ge_s');

QAST::MASTOperations.add_core_moarop_mapping('cmp_I', 'cmp_I');
QAST::MASTOperations.add_core_moarop_mapping('iseq_I', 'eq_I');
QAST::MASTOperations.add_core_moarop_mapping('isne_I', 'ne_I');
QAST::MASTOperations.add_core_moarop_mapping('islt_I', 'lt_I');
QAST::MASTOperations.add_core_moarop_mapping('isle_I', 'le_I');
QAST::MASTOperations.add_core_moarop_mapping('isgt_I', 'gt_I');
QAST::MASTOperations.add_core_moarop_mapping('isge_I', 'ge_I');

# aggregate opcodes
QAST::MASTOperations.add_core_moarop_mapping('atpos', 'atpos_o');
QAST::MASTOperations.add_core_moarop_mapping('atpos_i', 'atpos_i');
QAST::MASTOperations.add_core_moarop_mapping('atpos_n', 'atpos_n');
QAST::MASTOperations.add_core_moarop_mapping('atpos_s', 'atpos_s');
QAST::MASTOperations.add_core_moarop_mapping('atkey', 'atkey_o');
QAST::MASTOperations.add_core_moarop_mapping('atkey_i', 'atkey_i');
QAST::MASTOperations.add_core_moarop_mapping('atkey_n', 'atkey_n');
QAST::MASTOperations.add_core_moarop_mapping('atkey_s', 'atkey_s');
QAST::MASTOperations.add_core_moarop_mapping('bindpos', 'bindpos_o', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindpos_i', 'bindpos_i', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindpos_n', 'bindpos_n', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindpos_s', 'bindpos_s', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindkey', 'bindkey_o', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindkey_i', 'bindkey_i', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindkey_n', 'bindkey_n', 2);
QAST::MASTOperations.add_core_moarop_mapping('bindkey_s', 'bindkey_s', 2);
QAST::MASTOperations.add_core_moarop_mapping('existskey', 'existskey');
QAST::MASTOperations.add_core_moarop_mapping('deletekey', 'deletekey');
QAST::MASTOperations.add_core_moarop_mapping('elems', 'elems');
QAST::MASTOperations.add_core_moarop_mapping('setelems', 'setelemspos', 0);
QAST::MASTOperations.add_core_moarop_mapping('existspos', 'existspos');
QAST::MASTOperations.add_core_moarop_mapping('push', 'push_o', 1);
QAST::MASTOperations.add_core_moarop_mapping('push_i', 'push_i', 1);
QAST::MASTOperations.add_core_moarop_mapping('push_n', 'push_n', 1);
QAST::MASTOperations.add_core_moarop_mapping('push_s', 'push_s', 1);
QAST::MASTOperations.add_core_moarop_mapping('pop', 'pop_o');
QAST::MASTOperations.add_core_moarop_mapping('pop_i', 'pop_i');
QAST::MASTOperations.add_core_moarop_mapping('pop_n', 'pop_n');
QAST::MASTOperations.add_core_moarop_mapping('pop_s', 'pop_s');
QAST::MASTOperations.add_core_moarop_mapping('unshift', 'unshift_o', 1);
QAST::MASTOperations.add_core_moarop_mapping('unshift_i', 'unshift_i', 1);
QAST::MASTOperations.add_core_moarop_mapping('unshift_n', 'unshift_n', 1);
QAST::MASTOperations.add_core_moarop_mapping('unshift_s', 'unshift_s', 1);
QAST::MASTOperations.add_core_moarop_mapping('shift', 'shift_o');
QAST::MASTOperations.add_core_moarop_mapping('shift_i', 'shift_i');
QAST::MASTOperations.add_core_moarop_mapping('shift_n', 'shift_n');
QAST::MASTOperations.add_core_moarop_mapping('shift_s', 'shift_s');
QAST::MASTOperations.add_core_moarop_mapping('splice', 'splice');
QAST::MASTOperations.add_core_moarop_mapping('islist', 'islist');
QAST::MASTOperations.add_core_moarop_mapping('ishash', 'ishash');
QAST::MASTOperations.add_core_moarop_mapping('iterator', 'iter');
QAST::MASTOperations.add_core_moarop_mapping('iterkey_s', 'iterkey_s');
QAST::MASTOperations.add_core_moarop_mapping('iterval', 'iterval');

# object opcodes
QAST::MASTOperations.add_core_moarop_mapping('null', 'null');
QAST::MASTOperations.add_core_moarop_mapping('null_s', 'null_s');
QAST::MASTOperations.add_core_moarop_mapping('what', 'getwhat');
QAST::MASTOperations.add_core_moarop_mapping('how', 'gethow');
QAST::MASTOperations.add_core_moarop_mapping('who', 'getwho');
QAST::MASTOperations.add_core_moarop_mapping('where', 'getwhere');
QAST::MASTOperations.add_core_moarop_mapping('findmethod', 'findmeth_s');
QAST::MASTOperations.add_core_moarop_mapping('setwho', 'setwho');
QAST::MASTOperations.add_core_moarop_mapping('rebless', 'rebless');
QAST::MASTOperations.add_core_moarop_mapping('knowhow', 'knowhow');
QAST::MASTOperations.add_core_moarop_mapping('knowhowattr', 'knowhowattr');
QAST::MASTOperations.add_core_moarop_mapping('bootint', 'bootint');
QAST::MASTOperations.add_core_moarop_mapping('bootnum', 'bootnum');
QAST::MASTOperations.add_core_moarop_mapping('bootstr', 'bootstr');
QAST::MASTOperations.add_core_moarop_mapping('bootarray', 'bootarray');
QAST::MASTOperations.add_core_moarop_mapping('bootintarray', 'bootintarray');
QAST::MASTOperations.add_core_moarop_mapping('bootnumarray', 'bootnumarray');
QAST::MASTOperations.add_core_moarop_mapping('bootstrarray', 'bootstrarray');
QAST::MASTOperations.add_core_moarop_mapping('boothash', 'boothash');
QAST::MASTOperations.add_core_moarop_mapping('hlllist', 'hlllist');
QAST::MASTOperations.add_core_moarop_mapping('hllhash', 'hllhash');
QAST::MASTOperations.add_core_moarop_mapping('create', 'create');
QAST::MASTOperations.add_core_moarop_mapping('clone', 'clone');
QAST::MASTOperations.add_core_moarop_mapping('isconcrete', 'isconcrete');
QAST::MASTOperations.add_core_moarop_mapping('iscont', 'iscont');
QAST::MASTOperations.add_core_moarop_mapping('decont', 'decont');
QAST::MASTOperations.add_core_moarop_mapping('isnull', 'isnull');
QAST::MASTOperations.add_core_moarop_mapping('isnull_s', 'isnull_s');
QAST::MASTOperations.add_core_moarop_mapping('istrue', 'istrue');
QAST::MASTOperations.add_core_moarop_mapping('isfalse', 'isfalse');
QAST::MASTOperations.add_core_moarop_mapping('istype', 'istype');
QAST::MASTOperations.add_core_moarop_mapping('eqaddr', 'eqaddr');
QAST::MASTOperations.add_core_moarop_mapping('getattr', 'getattrs_o');
QAST::MASTOperations.add_core_moarop_mapping('getattr_i', 'getattrs_i');
QAST::MASTOperations.add_core_moarop_mapping('getattr_n', 'getattrs_n');
QAST::MASTOperations.add_core_moarop_mapping('getattr_s', 'getattrs_s');
QAST::MASTOperations.add_core_moarop_mapping('attrinited', 'attrinited');
QAST::MASTOperations.add_core_moarop_mapping('bindattr', 'bindattrs_o', 3);
QAST::MASTOperations.add_core_moarop_mapping('bindattr_i', 'bindattrs_i', 3);
QAST::MASTOperations.add_core_moarop_mapping('bindattr_n', 'bindattrs_n', 3);
QAST::MASTOperations.add_core_moarop_mapping('bindattr_s', 'bindattrs_s', 3);
QAST::MASTOperations.add_core_moarop_mapping('unbox_i', 'unbox_i');
QAST::MASTOperations.add_core_moarop_mapping('unbox_n', 'unbox_n');
QAST::MASTOperations.add_core_moarop_mapping('unbox_s', 'unbox_s');
QAST::MASTOperations.add_core_moarop_mapping('box_i', 'box_i');
QAST::MASTOperations.add_core_moarop_mapping('box_n', 'box_n');
QAST::MASTOperations.add_core_moarop_mapping('box_s', 'box_s');
QAST::MASTOperations.add_core_moarop_mapping('can', 'can_s');
QAST::MASTOperations.add_core_moarop_mapping('reprname', 'reprname');
QAST::MASTOperations.add_core_moarop_mapping('newtype', 'newtype');
QAST::MASTOperations.add_core_moarop_mapping('composetype', 'composetype');
QAST::MASTOperations.add_core_moarop_mapping('setboolspec', 'setboolspec', 0);
QAST::MASTOperations.add_core_moarop_mapping('setmethcache', 'setmethcache', 0);
QAST::MASTOperations.add_core_moarop_mapping('setmethcacheauth', 'setmethcacheauth', 0);
QAST::MASTOperations.add_core_moarop_mapping('settypecache', 'settypecache', 0);
QAST::MASTOperations.add_core_moarop_mapping('isinvokable', 'isinvokable');
QAST::MASTOperations.add_core_moarop_mapping('setinvokespec', 'setinvokespec', 0);
QAST::MASTOperations.add_core_moarop_mapping('setcontspec', 'setcontspec', 0);
QAST::MASTOperations.add_core_moarop_mapping('assign', 'assign', 0);
QAST::MASTOperations.add_core_moarop_mapping('assignunchecked', 'assignunchecked', 0);

# defined - overridden by HLL, but by default same as .DEFINITE.
QAST::MASTOperations.add_core_moarop_mapping('defined', 'isconcrete');

# lexical related opcodes
QAST::MASTOperations.add_core_moarop_mapping('getlexdyn', 'getdynlex');
QAST::MASTOperations.add_core_moarop_mapping('bindlexdyn', 'binddynlex');
QAST::MASTOperations.add_core_op('locallifetime', -> $qastcomp, $op {
    # TODO: take advantage of this info for code-gen, if possible.
    $qastcomp.as_mast($op[0])
});

# code object related opcodes
QAST::MASTOperations.add_core_moarop_mapping('takeclosure', 'takeclosure');
QAST::MASTOperations.add_core_moarop_mapping('getcodeobj', 'getcodeobj');
QAST::MASTOperations.add_core_moarop_mapping('setcodeobj', 'setcodeobj', 0);
QAST::MASTOperations.add_core_moarop_mapping('getcodename', 'getcodename');
QAST::MASTOperations.add_core_moarop_mapping('setcodename', 'setcodename', 0);
QAST::MASTOperations.add_core_moarop_mapping('forceouterctx', 'forceouterctx', 0);
QAST::MASTOperations.add_core_op('setup_blv', -> $qastcomp, $op {
    if +@($op) != 1 || !nqp::ishash($op[0]) {
        nqp::die('setup_blv requires one hash operand');
    }

    my @ops;
    for $op[0] {
        my $frame     := %*MAST_FRAMES{$_.key};
        my $block_reg := $*REGALLOC.fresh_register($MVM_reg_obj);
        push_op(@ops, 'getcode', $block_reg, $frame);
        for $_.value -> @lex {
            my $valres := $qastcomp.as_mast(QAST::WVal.new( :value(@lex[1]) ));
            push_ilist(@ops, $valres);
            push_op(@ops, 'setlexvalue', $block_reg, MAST::SVal.new( :value(@lex[0]) ),
                $valres.result_reg, MAST::IVal.new( :value(@lex[2]) ));
            $*REGALLOC.release_register($valres.result_reg, $MVM_reg_obj);
        }
        $*REGALLOC.release_register($block_reg, $MVM_reg_obj);
    }

    MAST::InstructionList.new(@ops, $*REGALLOC.fresh_o(), $MVM_reg_obj)
});

# language/compiler ops
QAST::MASTOperations.add_core_moarop_mapping('getcomp', 'getcomp');
QAST::MASTOperations.add_core_moarop_mapping('bindcomp', 'bindcomp');
QAST::MASTOperations.add_core_moarop_mapping('gethllsym', 'gethllsym');
QAST::MASTOperations.add_core_moarop_mapping('getcurhllsym', 'getcurhllsym');
QAST::MASTOperations.add_core_moarop_mapping('bindcurhllsym', 'bindcurhllsym');
QAST::MASTOperations.add_core_moarop_mapping('sethllconfig', 'sethllconfig');
QAST::MASTOperations.add_core_moarop_mapping('loadbytecode', 'loadbytecode');

# regex engine related opcodes
QAST::MASTOperations.add_core_moarop_mapping('nfafromstatelist', 'nfafromstatelist');
QAST::MASTOperations.add_core_moarop_mapping('nfarunproto', 'nfarunproto');
QAST::MASTOperations.add_core_moarop_mapping('nfarunalt', 'nfarunalt', 0);

# process related opcodes
QAST::MASTOperations.add_core_moarop_mapping('exit', 'exit', 0);
QAST::MASTOperations.add_core_moarop_mapping('sleep', 'sleep');
QAST::MASTOperations.add_core_moarop_mapping('getenvhash', 'getenvhash');
QAST::MASTOperations.add_core_moarop_mapping('getenv', 'getenv');
QAST::MASTOperations.add_core_moarop_mapping('setenv', 'setenv');
QAST::MASTOperations.add_core_moarop_mapping('delenv', 'delenv');

sub resolve_condition_op($kind, $negated) {
    return $negated ??
        $kind == $MVM_reg_int64 ?? 'unless_i' !!
        $kind == $MVM_reg_num64 ?? 'unless_n' !!
        $kind == $MVM_reg_str   ?? 'unless_s0' !!
        $kind == $MVM_reg_obj   ?? 'unless_o' !!
        nqp::die("unhandled kind $kind")
     !! $kind == $MVM_reg_int64 ?? 'if_i' !!
        $kind == $MVM_reg_num64 ?? 'if_n' !!
        $kind == $MVM_reg_str   ?? 'if_s0' !!
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
