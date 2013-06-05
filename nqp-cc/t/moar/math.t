#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.6643));
        op(@ins, 'sin_n', $r0, $r0);
        op(@ins, 'coerce_ns', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "-0.994766\n",
    "num sin");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.6643));
        op(@ins, 'cos_n', $r0, $r0);
        op(@ins, 'coerce_ns', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "0.102176\n",
    "num cos");
