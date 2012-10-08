#!nqp
use MASTTesting;

plan(1);

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
            op(@ins, 'const_s', $r0, sval('In new thread'));
            op(@ins, 'say_s', $r0);
            op(@ins, 'const_i64', $r1, ival(1000000));
            op(@ins, 'sleep', $r1);
            op(@ins, 'const_s', $r0, sval('In new thread after sleep'));
            op(@ins, 'say_s', $r0);
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
        
        op(@ins, 'const_s', $str, sval('Before thread'));
        op(@ins, 'say_s', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread, $code, $type);
        op(@ins, 'const_i64', $time, ival(500000));
        op(@ins, 'sleep', $time);
        op(@ins, 'const_s', $str, sval('In main thread'));
        op(@ins, 'say_s', $str);
        op(@ins, 'const_i64', $time, ival(1000000));
        op(@ins, 'sleep', $time);
        op(@ins, 'const_s', $str, sval('In main thread at end'));
        op(@ins, 'say_s', $str);
        op(@ins, 'return');
    },
    "Before thread\nIn new thread\nIn main thread\nIn new thread after sleep\nIn main thread at end\n",
    "Can create a thread and it runs in parallel");
