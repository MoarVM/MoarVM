#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        op(@ins, 'return');
    },
    "",
    "just return", 1);
