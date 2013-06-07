#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := const($frame, sval("OutputMe\n"));
        my $r2 := const($frame, ival(0));
        my $r3 := const($frame, ival(-1));
        my $r4 := const($frame, ival(1));
        op(@ins, 'getstdout', $r0, $r4);
        op(@ins, 'close_fh', $r0);
        op(@ins, 'say', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "",
    "closing stdout");
# writing to stdout after it's been closed causes a crash on windows
# writing to stdout after it's been closed does nothing on linux

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r1 := local($frame, str);
        my $r2 := const($frame, ival(50));
        my $r3 := const($frame, sval("Makefile"));
        my $r4 := local($frame, NQPMu);
        my $r5 := const($frame, ival(0));
        my $r6 := const($frame, ival(11));
        my $r7 := const($frame, ival(7));
        my $r8 := const($frame, ival(1));
        op(@ins, 'open_fh', $r4, $r3, $r7, $r8);
        op(@ins, 'close_fh', $r4);
        op(@ins, 'say', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "alive\n",
    "close normal filehandle");
