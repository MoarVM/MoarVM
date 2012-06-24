#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, NQPMu);
        my $r2 := local($frame, NQPMu);
        my $r3 := local($frame, NQPMu);
        op(@ins, 'knowhow', $r1);
        op(@ins, 'findmeth', $r2, $r1, sval('new_type'));
        call(@ins, $r2, [$Arg::obj], $r1, :result($r1));
        op(@ins, 'const_i64', $r0, ival(100000000));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'create', $r3, $r1);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'if_i', $r0, $loop);
        op(@ins, 'return');
    },
    "",
    "creating and collecting 100 million objects", 1);
