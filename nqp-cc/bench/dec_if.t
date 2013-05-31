#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        op(@ins, 'const_i64', $r0, ival(100000000));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'if_i', $r0, $loop);
        op(@ins, 'return');
    },
    "",
    "decrement and compare 1e8 times", 1);
