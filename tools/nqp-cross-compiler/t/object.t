#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r1 := local($frame, NQPMu);
        
        
        op(@ins, 'say_s', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "alive\n",
    "object creation");
