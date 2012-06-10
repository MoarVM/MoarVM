#!nqp
use MASTTesting;

plan(4);

sub callee() {
    my $frame := MAST::Frame.new();
    my $r0 := MAST::Local.new(:index($frame.add_local(str)));
    my @ins := $frame.instructions;
    op(@ins, 'const_s', $r0, sval('OMG in callee!'));
    op(@ins, 'say_s', $r0);
    op(@ins, 'return');
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee();
        my $r0 := local($frame, str);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'const_s', $r0, sval('Before call'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :flags([])
            ));
        op(@ins, 'const_s', $r0, sval('After call'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "Before call\nOMG in callee!\nAfter call\n",
    "basic frame call");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee();
        my $r0 := local($frame, str);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'const_s', $r0, sval('Before call'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :flags([])
            ));
        op(@ins, 'const_s', $r0, sval('After call'));
        op(@ins, 'say_s', $r0);
        $cu.add_frame($callee);
    },
    "Before call\nOMG in callee!\nAfter call\n",
    "frame call with no return in the first frame");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee();
        nqp::pop($callee.instructions);
        my $r0 := local($frame, str);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'const_s', $r0, sval('Before call'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :flags([])
            ));
        op(@ins, 'const_s', $r0, sval('After call'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "Before call\nOMG in callee!\nAfter call\n",
    "frame call with no return in the callee frame");

sub callee_ret_i() {
    my $frame := MAST::Frame.new();
    my $r0 := MAST::Local.new(:index($frame.add_local(int)));
    my @ins := $frame.instructions;
    op(@ins, 'const_i64', $r0, ival(2000));
    op(@ins, 'return_i', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_ret_i();
        my $r0 := local($frame, int);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :result($r0),
                :flags([])
            ));
        op(@ins, 'say_i', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "2000\n",
    "call returning int");
