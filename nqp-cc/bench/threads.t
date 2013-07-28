#!nqp
use MASTTesting;

plan(1);

my $num_threads := 10 * 1000;

sub make_thread_type($frame) {
    my @ins := $frame.instructions;
    my $name := local($frame, str);
    my $repr := local($frame, str);
    my $how  := local($frame, NQPMu);
    my $type := local($frame, NQPMu);
    my $meth := local($frame, NQPMu);

    # Create the type.
    op(@ins, 'const_s', $name, sval('TestThreadType'));
    op(@ins, 'const_s', $repr, sval('MVMThread'));
    op(@ins, 'knowhow', $how);
    op(@ins, 'findmeth', $meth, $how, sval('new_type'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::str],
        $how, sval('name'), $name, sval('repr'), $repr, :result($type));

    # Compose.
    op(@ins, 'gethow', $how, $type);
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));

    $type
}

mast_frame_output_is(-> $frame, @ins, $cu {
        sub thread_code() {
            my $frame := MAST::Frame.new();
            my $r0 := local($frame, str);
            my $r1 := local($frame, int);
            my @ins := $frame.instructions;
            op(@ins, 'return');
            return $frame;
        }

        my $type := make_thread_type($frame);

        my $thread_code := thread_code();
        $cu.add_frame($thread_code);

        my $code   := local($frame, NQPMu);
        my $thread := local($frame, NQPMu);
        my $str    := local($frame, str);
        my $time   := local($frame, int);
        my $c      := const($frame, ival($num_threads));

        op(@ins, 'getcode', $code, $thread_code);
        nqp::push(@ins, label('loop'));
        op(@ins, 'newthread', $thread, $code, $type);
        op(@ins, 'dec_i', $c);
        op(@ins, 'if_i', $c, label('loop'));
        op(@ins, 'return');
    },
    "",
    "Can create $num_threads threads");
