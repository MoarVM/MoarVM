#!nqp
use MASTTesting;

plan(2);

sub callee() {
    my $frame := MAST::Frame.new();
    my @ins := $frame.instructions;
    op(@ins, 'return');
    return $frame;
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        op(@ins, 'const_i64', $counter, ival(30 *1000 *1000));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
    },
    "",
    "test timing of lots of incrementing", 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $func := local($frame, NQPMu);
        my $callee := callee();
        op(@ins, 'const_i64', $counter, ival(30 *1000 *1000));
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
