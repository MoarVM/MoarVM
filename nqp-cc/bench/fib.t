#!nqp
use MASTTesting;

plan(1);

sub fibsub() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my $r1 := local($frame, int);
    my $r2 := local($frame, int);
    my $r3 := local($frame, NQPMu);
    my $two := const($frame, ival(2));
    my @ins := $frame.instructions;
    my $skip_return := label('1');

    op(@ins, 'checkarity', ival(1), ival(1));
    op(@ins, 'param_rp_i', $r0, ival(0));
    op(@ins, 'lt_i', $r1, $r0, $two);
    op(@ins, 'unless_i', $r1, $skip_return);
    op(@ins, 'return_i', $r0);
    nqp::push(@ins, $skip_return);
    op(@ins, 'set', $r2, $r0);
    op(@ins, 'set', $r1, $r0);
    op(@ins, 'dec_i', $r2);
    op(@ins, 'dec_i', $r1);
    op(@ins, 'dec_i', $r1);

    op(@ins, 'getcode', $r3, $frame);
    call(@ins, $r3, [$Arg::int], $r1, :result($r1));
    call(@ins, $r3, [$Arg::int], $r2, :result($r2));

    op(@ins, 'add_i', $r2, $r1, $r2);
    op(@ins, 'return_i', $r2);
    return $frame;
}

sub runfib($n) {

    mast_frame_output_is(-> $frame, @ins, $cu {
        my $fibsub := fibsub();

        my $r0 := const($frame, ival($n));
        my $r3 := local($frame, NQPMu);
        my $str := local($frame, str);
        op(@ins, 'getcode', $r3, $fibsub);
        call(@ins, $r3, [$Arg::int], $r0, :result($r0));
        op(@ins, 'coerce_is', $str, $r0);
        op(@ins, 'say', $str);
        op(@ins, 'return');
        $cu.add_frame($fibsub);
    },
    "",
    "recursive fi $n", 1);
}

runfib(35);
