use MASTCompiler;
use QASTCompilerMAST;

sub MAIN(*@ARGS) {
    my $nqpcomp := pir::compreg__Ps('nqp');
    
    $nqpcomp.stages(< start parse past mast mbc moar >);
    $nqpcomp.HOW.add_method($nqpcomp, 'mast', method ($qast, *%adverbs) {
        QAST::MASTCompiler.to_mast($qast);
    });
    $nqpcomp.HOW.add_method($nqpcomp, 'mbc', method ($mast, *%adverbs) {
        MAST::Compiler.compile($mast, 'temp.moarvm');
    });
    $nqpcomp.HOW.add_method($nqpcomp, 'moar', method ($class, *%adverbs) {
        -> {
            pir::spawnw__Is("del /? >temp.output 2>&1");
            my $out := slurp('temp.output');
            if (!($out ~~ /Extensions/)) {
                pir::spawnw__Is("../../moarvm temp.moarvm");
            }
            else {
                pir::spawnw__Is("..\\..\\moarvm temp.moarvm");
            }
        }
    });
    
    $nqpcomp.command_line(@ARGS, :precomp(1), :encoding('utf8'), :transcode('ascii iso-8859-1'));
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
    nqp::die("nqp numify op NYI");
});

$ops.add_hll_op('nqp', 'stringify', -> $qastcomp, $op {
    nqp::die("nqp stringify op NYI");
});

$ops.add_hll_op('nqp', 'falsey', -> $qastcomp, $op {
    nqp::die("nqp falsey op NYI");
});
