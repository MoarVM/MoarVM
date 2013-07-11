#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r1 := local($frame, str);
        my $r2 := const($frame, ival(50));
        my $r3 := const($frame, sval("Makefile"));
        my $r4 := local($frame, NQPMu);
        my $r5 := const($frame, ival(0));
        my $r6 := const($frame, ival(11));
        my $r7 := const($frame, ival(1));
        my $r8 := const($frame, ival(1));
        op(@ins, 'open_fh', $r4, $r3, $r7, $r8);
        op(@ins, 'read_fhs', $r3, $r4, $r2);
        op(@ins, 'substr_s', $r3, $r3, $r5, $r6);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "# Copyright\n",
    "open file for reading");
