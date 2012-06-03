#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('OMG strings!'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "OMG strings!\n",
    "string constant loading");
