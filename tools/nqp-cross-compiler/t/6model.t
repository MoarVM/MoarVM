#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        op(@ins, 'getstdout', $r0);
        op(@ins, 'reprid', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "7\n",
    "get reprid");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        op(@ins, 'getstdout', $r0);
        op(@ins, 'concrete', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "1\n",
    "is concrete");
