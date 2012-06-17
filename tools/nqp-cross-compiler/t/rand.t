#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, num);
        op(@ins, 'const_i64', $r0, ival(1000));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'rand_n', $r2);
        op(@ins, 'say_n', $r2);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'if_i', $r0, $loop);
        op(@ins, 'return');
    },
    "",
    "generate a lot of random numbers", 1);
