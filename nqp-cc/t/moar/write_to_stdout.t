#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := const($frame, sval("OutputMe\n"));
        my $r2 := const($frame, ival(0));
        my $r3 := const($frame, ival(-1));
        my $r7 := const($frame, ival(1));
        op(@ins, 'getstdout', $r0);
        op(@ins, 'write_fhs', $r7, $r0, $r1, $r2, $r3);
        op(@ins, 'return');
    },
    "OutputMe\n",
    "write to stdout oshandle");
