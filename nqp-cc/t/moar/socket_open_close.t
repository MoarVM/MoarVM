#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        op(@ins, 'anonoshtype', $r0);
        my $r7 := const($frame, ival(1));
        op(@ins, 'connect_sk', $r0, $r0, const($frame, sval("www.microsoft.com")), const($frame, ival(80)), const($frame, ival(6)), $r7);
        op(@ins, 'close_sk', $r0);
        op(@ins, 'say', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "alive\n",
    "socket open close");
