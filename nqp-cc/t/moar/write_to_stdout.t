#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, NQPMu);
        my $r1 := const($frame, sval("OutputMe\n"));
        my $r7 := const($frame, ival(1));
        op(@ins, 'getstdout', $r0);
        op(@ins, 'write_fhs', $r7, $r0, $r1);
        op(@ins, 'return');
    },
    "OutputMe\n",
    "write to stdout oshandle");
