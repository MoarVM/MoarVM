use MASTCompiler;
use QASTCompilerMAST;

# register kinds (for :want)
my $MVM_reg_void            := 0; # not really a register; just a result/return kind marker
my $MVM_reg_int8            := 1;
my $MVM_reg_int16           := 2;
my $MVM_reg_int32           := 3;
my $MVM_reg_int64           := 4;
my $MVM_reg_num32           := 5;
my $MVM_reg_num64           := 6;
my $MVM_reg_str             := 7;
my $MVM_reg_obj             := 8;

# Backend class for MoarVM cross-compiler.
class HLL::Backend::MoarVM {
    method apply_transcodings($s, $transcode) {
        $s
    }

    method config() {
        nqp::hash()
    }

    method force_gc() {
        nqp::die("Cannot force GC on MoarVM backend yet");
    }

    method name() {
        'moar'
    }

    method nqpevent($spec?) {
        # Doesn't do anything just yet
    }

    method run_profiled($what) {
        nqp::die("No profiling support on MoarVM");
    }

    method run_traced($level, $what) {
        nqp::die("No tracing support on MoarVM");
    }

    method version_string() {
        "MoarVM"
    }

    method stages() {
        'mast mbc moar'
    }

    method mast($qast, *%adverbs) {
        QAST::MASTCompiler.to_mast($qast);
    }

    method mbc($mast, *%adverbs) {
        my $output := %adverbs<output> || 'temp.moarvm';
        MAST::Compiler.compile($mast, $output);
        # XXX Following is a hack.
        if %adverbs<target> eq 'mbc' {
            nqp::exit(0);
        }
    }

    method moar($class, *%adverbs) {
        -> {
#?if parrot
            my %conf := pir::getinterp__P()[pir::const::IGLOBALS_CONFIG_HASH];
            my $os := %conf<platform>;
#?endif
#?if jvm
#            my %conf := nqp::jvmgetproperties(); XXX uncomment when fudging works
#            my $os := %conf<os.name>;
#?endif
            if nqp::lc($os) ~~ /^(win|mswin)/ {
                pir::spawnw__Is("..\\moarvm temp.moarvm");
            }
            else {
                pir::spawnw__Is("../moarvm temp.moarvm");
            }
        }
    }

    method is_precomp_stage($stage) {
        # Currently, everything is pre-comp since we're a cross-compiler.
        1
    }

    method is_textual_stage($stage) {
        0
    }

    method is_compunit($cuish) {
        !pir::isa__IPs($cuish, 'String')
    }

    method compunit_mainline($cuish) {
        $cuish
    }
}

sub MAIN(*@ARGS) {
    # Get original compiler, then re-register it as a cross compiler.
    my $nqpcomp-orig := nqp::getcomp('nqp');
    my $nqpcomp-cc   := nqp::clone($nqpcomp-orig);
    $nqpcomp-cc.language('nqp-cc');

    # Set backend and run.
    $nqpcomp-cc.backend(HLL::Backend::MoarVM);
    $nqpcomp-cc.command_line(@ARGS, :stable-sc(1),
        :setting('NQPCOREMoar'),
        :custom-regex-lib('QRegexMoar'),
        :encoding('utf8'), :transcode('ascii iso-8859-1'));
}

# Set up various NQP-specific ops.
my $ops := QAST::MASTCompiler.operations();

$ops.add_hll_op('nqp', 'preinc', -> $qastcomp, $op {
    my $var := $op[0];
    unless nqp::istype($var, QAST::Var) {
        nqp::die("Pre-increment can only work on a variable");
    }
    $qastcomp.as_mast(QAST::Op.new(
        :op('bind'),
        $var,
        QAST::Op.new(
            :op('add_n'),
            $var,
            QAST::IVal.new( :value(1) )
        )));
});

$ops.add_hll_op('nqp', 'predec', -> $qastcomp, $op {
    my $var := $op[0];
    unless nqp::istype($var, QAST::Var) {
        nqp::die("Pre-decrement can only work on a variable");
    }
    $qastcomp.as_mast(QAST::Op.new(
        :op('bind'),
        $var,
        QAST::Op.new(
            :op('sub_n'),
            $var,
            QAST::IVal.new( :value(1) )
        )));
});

$ops.add_hll_op('nqp', 'postinc', -> $qastcomp, $op {
    my $var := $op[0];
    my $tmp := QAST::Op.unique('tmp');
    unless nqp::istype($var, QAST::Var) {
        nqp::die("Post-increment can only work on a variable");
    }
    $qastcomp.as_mast(QAST::Stmt.new(
        :resultchild(0),
        QAST::Op.new(
            :op('bind'),
            QAST::Var.new( :name($tmp), :scope('local'), :decl('var'), :returns($var.returns) ),
            $var
        ),
        QAST::Op.new(
            :op('bind'),
            $var,
            QAST::Op.new(
                :op('add_n'),
                QAST::Var.new( :name($tmp), :scope('local'), :returns($var.returns)  ),
                QAST::IVal.new( :value(1) )
            )
        )));
});

$ops.add_hll_op('nqp', 'postdec', -> $qastcomp, $op {
    my $var := $op[0];
    my $tmp := QAST::Op.unique('tmp');
    unless nqp::istype($var, QAST::Var) {
        nqp::die("Post-decrement can only work on a variable");
    }
    $qastcomp.as_mast(QAST::Stmt.new(
        :resultchild(0),
        QAST::Op.new(
            :op('bind'),
            QAST::Var.new( :name($tmp), :scope('local'), :decl('var') ),
            $var
        ),
        QAST::Op.new(
            :op('bind'),
            $var,
            QAST::Op.new(
                :op('sub_n'),
                QAST::Var.new( :name($tmp), :scope('local') ),
                QAST::IVal.new( :value(1) )
            )
        )));
});

$ops.add_hll_op('nqp', 'numify', -> $qastcomp, $op {
    $qastcomp.as_mast($op[0], :want($MVM_reg_num64))
});

$ops.add_hll_op('nqp', 'stringify', -> $qastcomp, $op {
    $qastcomp.as_mast($op[0], :want($MVM_reg_str))
});

$ops.add_hll_op('nqp', 'falsey', -> $qastcomp, $op {
    unless $op.list == 1 {
        nqp::die('falsey op requires one child');
    }
    my $val := $qastcomp.as_mast($op[0]);
    if $val.result_kind == $MVM_reg_int64 {
        my $not_reg := $*REGALLOC.fresh_register($MVM_reg_int64);
        my @ins := $val.instructions;
        push_op(@ins, 'not_i', $not_reg, $val.result_reg);
        MAST::InstructionList.new(@ins, $not_reg, $MVM_reg_int64)
    }
    elsif $val.result_kind == $MVM_reg_obj {
        my $not_reg := $*REGALLOC.fresh_register($MVM_reg_int64);
        my @ins := $val.instructions;
        push_op(@ins, 'isfalse', $not_reg, $val.result_reg);
        MAST::InstructionList.new(@ins, $not_reg, $MVM_reg_int64)
    }
    elsif $val.result_kind == $MVM_reg_str {
        my $not_reg := $*REGALLOC.fresh_register($MVM_reg_int64);
        my @ins := $val.instructions;
        push_op(@ins, 'isfalse_s', $not_reg, $val.result_reg);
        MAST::InstructionList.new(@ins, $not_reg, $MVM_reg_int64)
    }
    else {
        nqp::die("This case of nqp falsey op NYI");
    }
});

# NQP object unbox.
QAST::MASTOperations.add_hll_unbox('nqp', $MVM_reg_int64, -> $qastcomp, $reg {
    my $il := nqp::list();
    my $a := $*REGALLOC.fresh_register($MVM_reg_num64);
    my $b := $*REGALLOC.fresh_register($MVM_reg_int64);
    push_op($il, 'smrt_numify', $a, $reg);
    push_op($il, 'coerce_ni', $b, $a);
    $*REGALLOC.release_register($a, $MVM_reg_num64);
    $*REGALLOC.release_register($reg, $MVM_reg_obj);
    MAST::InstructionList.new($il, $b, $MVM_reg_int64)
});

QAST::MASTOperations.add_hll_unbox('nqp', $MVM_reg_num64, -> $qastcomp, $reg {
    my $il := nqp::list();
    my $res_reg := $*REGALLOC.fresh_register($MVM_reg_num64);
    push_op($il, 'smrt_numify', $res_reg, $reg);
    $*REGALLOC.release_register($reg, $MVM_reg_obj);
    MAST::InstructionList.new($il, $res_reg, $MVM_reg_num64)
});

QAST::MASTOperations.add_hll_unbox('nqp', $MVM_reg_str, -> $qastcomp, $reg {
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

QAST::MASTOperations.add_hll_box('nqp', $MVM_reg_int64, boxer($MVM_reg_int64, 'hllboxtype_i', 'box_i'));
QAST::MASTOperations.add_hll_box('nqp', $MVM_reg_num64, boxer($MVM_reg_num64, 'hllboxtype_n', 'box_n'));
QAST::MASTOperations.add_hll_box('nqp', $MVM_reg_str, boxer($MVM_reg_str, 'hllboxtype_s', 'box_s'));

sub push_op(@dest, $op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        next if ~$_ eq '$allops';
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);

    nqp::push(@dest, MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    ));
}
