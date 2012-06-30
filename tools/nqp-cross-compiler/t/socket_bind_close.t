#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        op(@ins, 'anonoshtype', $r0);
        op(@ins, 'bind_sk', $r0, $r0, const($frame, sval("0.0.0.0")), const($frame, ival(33245)), const($frame, ival(6)));
        op(@ins, 'close_sk', $r0);
        op(@ins, 'say_s', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "alive\n",
    "socket bind close");
