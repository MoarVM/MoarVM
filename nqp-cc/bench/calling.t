#!nqp
use MASTTesting;

plan(3);

sub callee() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my @ins := $frame.instructions;
    op(@ins, 'const_i64', $r0, ival(235234986235));
    op(@ins, 'return');
    return $frame;
}
sub recursive() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my $func := local($frame, NQPMu);
    my @ins := $frame.instructions;
    my $ret := label('ret');
    op(@ins, 'checkarity', ival(1), ival(1));
    op(@ins, 'param_rp_i', $r0, ival(0));
    op(@ins, 'dec_i', $r0);
    op(@ins, 'unless_i', $r0, $ret);
    op(@ins, 'getcode', $func, $frame);
    call(@ins, $func, [$Arg::int], $r0, :result($r0));
    nqp::push(@ins, $ret);
    op(@ins, 'return');
    return $frame;

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $divisor := local($frame, int);
        my $modulus := local($frame, int);
        my $sleepms := local($frame, int);
        op(@ins, 'const_i64', $divisor, ival(10 *1000 *1000));
        op(@ins, 'const_i64', $counter, ival(100 *1000 *1000));
        op(@ins, 'const_i64', $sleepms, ival(1000 *1000)); # microseconds
        my $loop := label('loop');
        my $skipsleep := label('skipsleep');
        nqp::push(@ins, $loop);
        op(@ins, 'mod_i', $modulus, $counter, $divisor);
        op(@ins, 'if_i', $modulus, $skipsleep);
        op(@ins, 'sleep', $sleepms);
        nqp::push(@ins, $skipsleep);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
    },
    "",
    "test memory usage of lots of incrementing and sleeping", 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $divisor := local($frame, int);
        my $modulus := local($frame, int);
        my $sleepms := local($frame, int);
        my $func := local($frame, NQPMu);
        my $callee := callee();

        op(@ins, 'const_i64', $divisor, ival(1 *1000 *1000));
        op(@ins, 'const_i64', $counter, ival(10 *1000 *1000));
        op(@ins, 'const_i64', $sleepms, ival(2000 *1000)); # microseconds
        my $loop := label('loop');
        my $skipsleep := label('skipsleep');
        nqp::push(@ins, $loop);
        op(@ins, 'getcode', $func, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($func),
                :flags([])
            ));
        op(@ins, 'mod_i', $modulus, $counter, $divisor);
        op(@ins, 'if_i', $modulus, $skipsleep);
        op(@ins, 'sleep', $sleepms);
        nqp::push(@ins, $skipsleep);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "",
    "test memory usage of lots of calling", 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $func := local($frame, NQPMu);
        my $callee := recursive();

        op(@ins, 'const_i64', $r0, ival(10 *1000 *1000));
        op(@ins, 'getcode', $func, $callee);
        call(@ins, $func, [$Arg::int], $r0, :result($r0));
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "",
    "test memory usage of recursive calling", 1);
# 1.6GB in 3.6 s
