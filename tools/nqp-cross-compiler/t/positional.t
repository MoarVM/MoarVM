#!nqp
use MASTTesting;

plan(3);

sub array_type($frame) {
    my @ins := $frame.instructions;
    my $r0 := local($frame, str);
    my $r1 := local($frame, NQPMu);
    my $r2 := local($frame, NQPMu);
    op(@ins, 'const_s', $r0, sval('MVMArray'));
    op(@ins, 'knowhow', $r1);
    op(@ins, 'findmeth', $r2, $r1, sval('new_type'));
    call(@ins, $r2, [$Arg::obj, $Arg::named +| $Arg::str], $r1, sval('repr'), $r0, :result($r1));
    $r1
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $at := array_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        op(@ins, 'create', $r0, $at);
        op(@ins, 'elemspos', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "0\n",
    "New array has zero elements");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $at := array_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'create', $r0, $at);
        op(@ins, 'const_i64', $r2, ival(0));
        op(@ins, 'bindpos_o', $r0, $r2, $r0);
        op(@ins, 'elemspos', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'const_i64', $r2, ival(1));
        op(@ins, 'bindpos_o', $r0, $r2, $r0);
        op(@ins, 'elemspos', $r1, $r0);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "1\n2\n",
    "Adding elements increases element count");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $at := array_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, NQPMu);
        my $r4 := local($frame, NQPMu);
        my $r5 := local($frame, NQPMu);
        op(@ins, 'create', $r0, $at);
        op(@ins, 'create', $r3, $at);
        op(@ins, 'create', $r4, $at);
        op(@ins, 'const_i64', $r2, ival(0));
        op(@ins, 'bindpos_o', $r0, $r2, $r3);
        op(@ins, 'const_i64', $r2, ival(1));
        op(@ins, 'bindpos_o', $r0, $r2, $r4);
        op(@ins, 'const_i64', $r2, ival(0));
        op(@ins, 'atpos_o', $r5, $r0, $r2);
        op(@ins, 'eqaddr', $r1, $r5, $r3);
        op(@ins, 'say_i', $r1);
        op(@ins, 'eqaddr', $r1, $r5, $r4);
        op(@ins, 'say_i', $r1);
        op(@ins, 'const_i64', $r2, ival(1));
        op(@ins, 'atpos_o', $r5, $r0, $r2);
        op(@ins, 'eqaddr', $r1, $r5, $r3);
        op(@ins, 'say_i', $r1);
        op(@ins, 'eqaddr', $r1, $r5, $r4);
        op(@ins, 'say_i', $r1);
        op(@ins, 'return');
    },
    "1\n0\n0\n1\n",
    "Can retrieve items by index");
