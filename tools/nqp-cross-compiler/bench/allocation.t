#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, NQPMu);
        op(@ins, 'const_i64', $r0, ival(10000000));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'knowhow', $r1);
        op(@ins, 'create', $r1, $r1);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'if_i', $r0, $loop);
        op(@ins, 'return');
    },
    "",
    "creating and collecting 10 million objects", 1);
