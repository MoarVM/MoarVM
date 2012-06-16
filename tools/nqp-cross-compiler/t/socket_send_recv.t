#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := const($frame, sval("www.google.com")); # hostname
        my $r7 := const($frame, ival(80)); # port 80
        my $r8 := const($frame, ival(6)); # TCP
        my $r10 := const($frame, sval("GET / HTTP/1.0\r\n\r\n"));
        my $r11 := const($frame, ival(999999));
        op(@ins, 'anonoshtype', $r0);
        op(@ins, 'connect_sk', $r0, $r0, $r1, $r7, $r8);
        op(@ins, 'send_sks', $r0, $r10);
        op(@ins, 'recv_sks', $r1, $r0, $r11);
        op(@ins, 'close_sk', $r0);
#        op(@ins, 'say_s', $r1); # prints some html.  uncomment it to see :)
        op(@ins, 'say_s', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "alive\n",
    "socket send recv");
