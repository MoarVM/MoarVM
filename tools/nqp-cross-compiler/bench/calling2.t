#!nqp
use MASTTesting;

plan(3);

my $iter := 10 *1000 *1000;

sub callee() {
    my $frame := MAST::Frame.new();
    my @ins := $frame.instructions;
    op(@ins, 'return');
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        op(@ins, 'const_i64', $counter, ival($iter));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
    },
    "",
    "test timing of lots of incrementing", 1);
# 0.179 s at 10_000_000

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $func := local($frame, NQPMu);
        my $callee := callee();
        op(@ins, 'const_i64', $counter, ival($iter));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'getcode', $func, $callee);
        nqp::push(@ins, MAST::Call.new(
                :target($func),
                :flags([])
            ));
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "",
    "test timing of lots of calling", 1);
# 1.129 s at 10_000_000

sub callee2() {
    my $frame := MAST::Frame.new();
    my $r0 := local($frame, int);
    my $r1 := local($frame, int);
    my @ins := $frame.instructions;
    op(@ins, 'checkarity', ival(2), ival(2));
    op(@ins, 'param_rp_i', $r0, ival(0));
    op(@ins, 'param_rp_i', $r1, ival(1));
    op(@ins, 'return_i', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $func := local($frame, NQPMu);
        my $r0 := const($frame, ival(122));
        my $callee := callee2();
        op(@ins, 'const_i64', $counter, ival($iter));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'getcode', $func, $callee);
        call(@ins, $func, [$Arg::int, $Arg::int], $r0, $r0, :result($r0));
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "",
    "test timing of lots of calling with 2 args", 1);
# 3.763 s at 10_000_000

sub callee3() {
    my $frame := MAST::Frame.new();
    my $r0 := const($frame, ival(0));
    my @ins := $frame.instructions;
    op(@ins, 'return_i', $r0);
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $func := local($frame, NQPMu);
        my $r0 := const($frame, ival(122));
        my $callee := callee3();
        op(@ins, 'const_i64', $counter, ival($iter));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'getcode', $func, $callee);
        call(@ins, $func, [$Arg::int, $Arg::int], $r0, $r0, :result($r0));
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
        $cu.add_frame($callee);
    },
    "",
    "test timing of lots of calling with 2 args but not taking them", 1);
# 1.598 s at 10_000_000
