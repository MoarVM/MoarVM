#!nqp
use MASTTesting;

plan(11);

sub callee() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, str);
    my @ins := $frame.instructions;
    op(@ins, 'const_s', $r0, sval('OMG in callee!'));
    op(@ins, 'say', $r0);
    op(@ins, 'return');
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee();
        my $r0 := local($frame, str);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'const_s', $r0, sval('Before call'));
        op(@ins, 'say', $r0);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :flags([])
            ));
        op(@ins, 'const_s', $r0, sval('After call'));
        op(@ins, 'say', $r0);
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
        op(@ins, 'say', $r0);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :flags([])
            ));
        op(@ins, 'const_s', $r0, sval('After call'));
        op(@ins, 'say', $r0);
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
        op(@ins, 'say', $r0);
        op(@ins, 'getcode', $r1, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($r1),
                :flags([])
            ));
        op(@ins, 'const_s', $r0, sval('After call'));
        op(@ins, 'say', $r0);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "Before call\nOMG in callee!\nAfter call\n",
    "frame call with no return in the callee frame");

sub callee_ret_i() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my @ins := $frame.instructions;
    op(@ins, 'const_i64', $r0, ival(2000));
    op(@ins, 'return_i', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_ret_i();
        my $r0 := local($frame, int);
        my $r1 := local($frame, NQPMu);
        my $r2 := local($frame, str);
        op(@ins, 'getcode', $r1, $callee);
        call(@ins, $r1, [], :result($r0));
        op(@ins, 'coerce_is', $r2, $r0);
        op(@ins, 'say', $r2);
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
        my $r2 := local($frame, str);
        op(@ins, 'getcode', $r1, $callee);
        call(@ins, $r1, [], :result($r0));
        op(@ins, 'coerce_ns', $r2, $r0);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "34.1213\n",
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
        op(@ins, 'say', $r0);
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
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "KnowHOW\n",
    "method call on built-in object");

sub callee_subtracter() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my $r1 := local($frame, int);
    my $r2 := local($frame, int);
    my @ins := $frame.instructions;
    op(@ins, 'checkarity', ival(2), ival(2));
    op(@ins, 'param_rp_i', $r0, ival(0));
    op(@ins, 'param_rp_i', $r1, ival(1));
    op(@ins, 'sub_i', $r2, $r0, $r1);
    op(@ins, 'return_i', $r2);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_subtracter();
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, NQPMu);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(49));
        op(@ins, 'const_i64', $r1, ival(7));
        op(@ins, 'getcode', $r3, $callee);
        call(@ins, $r3, [$Arg::int, $Arg::int], $r0, $r1, :result($r2));
        op(@ins, 'coerce_is', $r4, $r2);
        op(@ins, 'say', $r4);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "42\n",
    "passing and receiving int parameters");

sub callee_multiplier_opt() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my $r1 := local($frame, int);
    my $r2 := local($frame, int);
    my $l0 := label('param_0');
    my @ins := $frame.instructions;
    op(@ins, 'checkarity', ival(1), ival(2));
    op(@ins, 'param_rp_i', $r0, ival(0));
    op(@ins, 'param_op_i', $r1, ival(1), $l0);
    op(@ins, 'const_i64', $r1, ival(2));
    nqp::push(@ins, $l0);
    op(@ins, 'mul_i', $r2, $r0, $r1);
    op(@ins, 'return_i', $r2);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_multiplier_opt();
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, NQPMu);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(10));
        op(@ins, 'const_i64', $r1, ival(7));
        op(@ins, 'getcode', $r3, $callee);
        call(@ins, $r3, [$Arg::int, $Arg::int], $r0, $r1, :result($r2));
        op(@ins, 'coerce_is', $r4, $r2);
        op(@ins, 'say', $r4);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "70\n",
    "optional parameter takes a passed value");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_multiplier_opt();
        my $r0 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, NQPMu);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(10));
        op(@ins, 'getcode', $r3, $callee);
        call(@ins, $r3, [$Arg::int], $r0, :result($r2));
        op(@ins, 'coerce_is', $r4, $r2);
        op(@ins, 'say', $r4);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "20\n",
    "optional parameter default setting code triggers");

sub callee_repeater() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, str);
    my $r1 := local($frame, int);
    my @ins := $frame.instructions;
    my $l0 := label('param_0');
    op(@ins, 'param_rn_s', $r0, sval('torepeat'));
    op(@ins, 'param_on_i', $r1, sval('count'), $l0);
    op(@ins, 'const_i64', $r1, ival(4));
    nqp::push(@ins, $l0);
    op(@ins, 'repeat_s', $r0, $r0, $r1);
    op(@ins, 'return_s', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $callee := callee_repeater();
        my $r2 := local($frame, str);
        my $r3 := local($frame, NQPMu);
        op(@ins, 'const_s', $r2, sval('hello'));
        op(@ins, 'getcode', $r3, $callee);
        call(@ins, $r3, [$Arg::named +| $Arg::str], sval('torepeat'), $r2, :result($r2));
        op(@ins, 'say', $r2);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "hellohellohellohello\n",
    "required and optional named parameters");
