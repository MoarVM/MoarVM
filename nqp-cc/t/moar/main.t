#!nqp
use MASTTesting;

plan(1);

# Set things up so that we are called from a main (which goes as the
# second frame).
mast_frame_output_is(-> $frame, @ins, $cu {
        # The main frame.
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('In first (but not main) frame'));
        op(@ins, 'say', $r0);
        op(@ins, 'return');

        # Create calling frame.
        my $main_frame := MAST::Frame.new();
        my @main_ins := $main_frame.instructions;
        my $main_r0 := local($main_frame, str);
        my $main_r1 := local($main_frame, NQPMu);
        op(@main_ins, 'const_s', $main_r0, sval('In main frame, making call'));
        op(@main_ins, 'say', $main_r0);
        op(@main_ins, 'getcode', $main_r1, $frame);
        nqp::push(@main_ins, MAST::Call.new(
                :target($main_r1),
                :flags([])
            ));
        op(@main_ins, 'const_s', $main_r0, sval('Back in main frame before end'));
        op(@main_ins, 'say', $main_r0);
        op(@main_ins, 'return');
        $cu.add_frame($main_frame);

        # Mark frame as main one.
        $cu.main_frame($main_frame);
    },
    "In main frame, making call\nIn first (but not main) frame\nBack in main frame before end\n",
    "main frame works");
