#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, str);
        op(@ins, 'anonoshtype', $r0);
        op(@ins, 'getstdout', $r0, $r0);
        op(@ins, 'reprname', $r1, $r0);
        op(@ins, 'say_s', $r1);
        op(@ins, 'return');
    },
    "MVMOSHandle\n",
    "get reprname");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        op(@ins, 'anonoshtype', $r0);
        op(@ins, 'isconcrete', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'getstdout', $r0, $r0);
        op(@ins, 'isconcrete', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "0\n1\n",
    "is concrete");
