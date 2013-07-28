#!nqp
use MASTTesting;

plan(5);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $l1 := label('foo');
        my $r2 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(1));
        op(@ins, 'goto', $l1);
        op(@ins, 'const_i64', $r0, ival(0));
        nqp::push(@ins, $l1);
        op(@ins, 'coerce_is', $r2, $r0);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "1\n",
    "unconditional forward branching");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $l1 := label('foo');
        my $l2 := label('bar');
        my $r3 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(1));
        op(@ins, 'goto', $l1);
        op(@ins, 'const_i64', $r0, ival(3));
        nqp::push(@ins, $l2);
        op(@ins, 'coerce_is', $r3, $r0);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
        nqp::push(@ins, $l1);
        op(@ins, 'const_i64', $r0, ival(2));
        op(@ins, 'goto', $l2);
    },
    "2\n",
    "unconditional forward and backward branching");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        my $loop := label('loop');
        my $loop_end := label('loop_end');
        op(@ins, 'const_i64', $r0, ival(5));
        op(@ins, 'const_i64', $r1, ival(0));
        nqp::push(@ins, $loop);
        op(@ins, 'unless_i', $r0, $loop_end);
        op(@ins, 'inc_i', $r1);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'goto', $loop);
        nqp::push(@ins, $loop_end);
        op(@ins, 'return');
    },
    "1\n2\n3\n4\n5\n",
    "conditional on zero integer branching");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        my $loop := label('loop');
        op(@ins, 'const_i64', $r0, ival(3));
        op(@ins, 'const_i64', $r1, ival(0));
        nqp::push(@ins, $loop);
        op(@ins, 'inc_i', $r1);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'if_i', $r0, $loop);
        op(@ins, 'return');
    },
    "1\n2\n3\n",
    "conditional on non-zero integer branching");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, str);
        my $l0 := label('l0');
        my $l1 := label('l1');
        my $l2 := label('l2');
        my $l3 := label('l3');
        op(@ins, 'const_n64', $r0, nval(0.0));
        op(@ins, 'const_n64', $r1, nval(1.0));

        # unless 0 don't say 1
        op(@ins, 'unless_n', $r0, $l0);
        op(@ins, 'coerce_ns', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, $l0);
        op(@ins, 'coerce_ns', $r2, $r0);
        op(@ins, 'say', $r2);

        # unless 1 don't say 1
        op(@ins, 'unless_n', $r1, $l1);
        op(@ins, 'coerce_ns', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, $l1);
        op(@ins, 'coerce_ns', $r2, $r0);
        op(@ins, 'say', $r2);

        # if 0 don't say 1
        op(@ins, 'if_n', $r0, $l2);
        op(@ins, 'coerce_ns', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, $l2);
        op(@ins, 'coerce_ns', $r2, $r0);
        op(@ins, 'say', $r2);

        # if 1 don't say 1
        op(@ins, 'if_n', $r1, $l3);
        op(@ins, 'coerce_ns', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, $l3);
        op(@ins, 'coerce_ns', $r2, $r0);
        op(@ins, 'say', $r2);

        op(@ins, 'return');
    },
    "0\n1\n0\n1\n0\n0\n",
    "conditional on zero and non-zero float branching");
