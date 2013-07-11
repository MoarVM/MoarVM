#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, str);
        my $r2 := const($frame, ival(1));
        op(@ins, 'getstdout', $r0);
        op(@ins, 'reprname', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "MVMOSHandle\n",
    "get reprname");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := const($frame, ival(1));
        my $str := local($frame, str);
        op(@ins, 'knowhow', $r0);
        op(@ins, 'isconcrete', $r1, $r0);
        op(@ins, 'coerce_is', $str, $r1);
        op(@ins, 'say', $str);
        op(@ins, 'getstdout', $r0);
        op(@ins, 'isconcrete', $r1, $r0);
        op(@ins, 'coerce_is', $str, $r1);
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "0\n1\n",
    "is concrete");
