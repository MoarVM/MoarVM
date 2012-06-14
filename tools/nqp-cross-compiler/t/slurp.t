#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := const($frame, sval('Makefile'));
        op(@ins, 'slurp', $r0, $r0);
        my $r1 := const($frame, ival(0));
        my $r2 := const($frame, ival(11));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "# Copyright\n",
    "slurp");
