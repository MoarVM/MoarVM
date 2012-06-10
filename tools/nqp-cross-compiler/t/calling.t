#!nqp
use MASTTesting;

plan(7);

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
        call(@ins, $r1, [], :result($r0));
        op(@ins, 'say_i', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "2000\n",
    "call returning int");

sub callee_ret_n() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, num);
    my @ins := $frame.instructions;
    op(@ins, 'const_n64', $r0, nval(34.1213));
    op(@ins, 'return_n', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_ret_n();
        my $r0 := local($frame, num);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'getcode', $r1, $callee);
        call(@ins, $r1, [], :result($r0));
        op(@ins, 'say_n', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "34.121300\n",
    "call returning num");

sub callee_ret_s() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, str);
    my @ins := $frame.instructions;
    op(@ins, 'const_s', $r0, sval("MOO MOO"));
    op(@ins, 'return_s', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_ret_s();
        my $r0 := local($frame, str);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'getcode', $r1, $callee);
        call(@ins, $r1, [], :result($r0));
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "MOO MOO\n",
    "call returning str");
    
mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, NQPMu);
        my $r2 := local($frame, NQPMu);
        my $r3 := local($frame, str);
        op(@ins, 'knowhow', $r0);
        op(@ins, 'gethow', $r1, $r0);
        op(@ins, 'findmeth', $r2, $r1, sval('name'));
        call(@ins, $r2, [$Arg::obj, $Arg::obj], $r1, $r0, :result($r3));
        op(@ins, 'say_s', $r3);
        op(@ins, 'return');
    },
    "KnowHOW\n",
    "method call on built-in object");
