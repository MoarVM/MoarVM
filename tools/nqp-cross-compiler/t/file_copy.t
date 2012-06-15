#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := const($frame, sval('Makefile'));
        my $r1 := const($frame, sval('Makefile2'));
        my $r2 := const($frame, ival(0));
        op(@ins, 'delete_f', $r1);
        op(@ins, 'copy_f', $r0, $r1);
        op(@ins, 'exists_f', $r2, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'delete_f', $r1);
        op(@ins, 'return');
    },
    "1\n",
    "file copy, exists, delete");
