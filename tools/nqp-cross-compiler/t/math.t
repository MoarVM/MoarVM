#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, num);
        op(@ins, 'const_n64', $r0, nval(23.6643));
        op(@ins, 'sin_n', $r0, $r0);
        op(@ins, 'say_n', $r0);
        op(@ins, 'return');
    },
    "-0.994766\n",
    "num sin");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, num);
        op(@ins, 'const_n64', $r0, nval(23.6643));
        op(@ins, 'cos_n', $r0, $r0);
        op(@ins, 'say_n', $r0);
        op(@ins, 'return');
    },
    "0.102176\n",
    "num cos");
