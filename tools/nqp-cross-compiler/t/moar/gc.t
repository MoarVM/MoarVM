#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, NQPMu);
        my $l0 := label('loop');
        op(@ins, 'const_i64', $r0, ival(100000));
        nqp::push(@ins, $l0);
        op(@ins, 'knowhow', $r1);
        op(@ins, 'create', $r1, $r1);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'if_i', $r0, $l0);
        op(@ins, 'say_i', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "lived creating loads of objects (so we shoulda GC'd)");
