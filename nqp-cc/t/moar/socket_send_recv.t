#!nqp
use MASTTesting;

plan(0);
nqp::exit(0);
# This test can't work on windows due to a libuv bug, see test-tcp-connect-error-after-write.c in libuv, and
# https://github.com/joyent/libuv/issues/444
mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := const($frame, sval("www.google.com")); # hostname
        my $r7 := const($frame, ival(80)); # port 80
        my $r8 := const($frame, ival(4)); # TCP
        my $r10 := const($frame, sval("GET / HTTP/1.0\r\n\r\n"));
        my $r11 := const($frame, ival(999999));
        my $r12 := const($frame, ival(1));
        my $r13 := const($frame, ival(0));
        my $r14 := const($frame, ival(-1));
        my $r15 := const($frame, ival(0));
        my $r16 := local($frame, str);
        op(@ins, 'connect_sk', $r0, $r1, $r7, $r8, $r12);
        op(@ins, 'send_sks', $r15, $r0, $r10, $r13, $r14);
        op(@ins, 'recv_sks', $r1, $r0, $r11);
        op(@ins, 'close_sk', $r0);
        op(@ins, 'graphs_s', $r7, $r1);
        op(@ins, 'gt_i', $r7, $r7, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r16, $r7);
        op(@ins, 'say', $r16);
#        op(@ins, 'say', $r1); # prints some html.  uncomment it to see :)
        op(@ins, 'say', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "1\nalive\n",
    "socket send recv");
