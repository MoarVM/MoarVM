#!nqp
use MASTTesting;

plan(6);

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
            op(@ins, 'say', $r0);
            op(@ins, 'const_i64', $r1, ival(1000000));
            op(@ins, 'sleep', $r1);
            op(@ins, 'const_s', $r0, sval('In new thread after sleep'));
            op(@ins, 'say', $r0);
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
        op(@ins, 'say', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread, $code, $type);
        op(@ins, 'const_i64', $time, ival(500000));
        op(@ins, 'sleep', $time);
        op(@ins, 'const_s', $str, sval('In main thread'));
        op(@ins, 'say', $str);
        op(@ins, 'const_i64', $time, ival(1000000));
        op(@ins, 'sleep', $time);
        op(@ins, 'const_s', $str, sval('In main thread at end'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "Before thread\nIn new thread\nIn main thread\nIn new thread after sleep\nIn main thread at end\n",
    "Can create a thread and it runs in parallel");

mast_frame_output_is(-> $frame, @ins, $cu {
        sub thread_code() {
            my $t_frame := MAST::Frame.new();
            $t_frame.set_outer($frame);
            my $r0 := local($t_frame, str);
            my $r1 := local($t_frame, int);
            my @ins := $t_frame.instructions;
            op(@ins, 'const_s', $r0, sval('In new thread'));
            op(@ins, 'say', $r0);
            op(@ins, 'getlex', $r0, MAST::Lexical.new( :index(0), :frames_out(1) ));
            op(@ins, 'say', $r0);
            op(@ins, 'return');
            return $t_frame;
        }

        my $type := make_thread_type($frame);

        my $thread_code := thread_code();
        $cu.add_frame($thread_code);

        my $code   := local($frame, NQPMu);
        my $thread := local($frame, NQPMu);
        my $str    := local($frame, str);
        my $time   := local($frame, int);
        my $lex := lexical($frame, str, '$lex');

        op(@ins, 'const_s', $str, sval('Lexical from outer'));
        op(@ins, 'bindlex', $lex, $str);
        op(@ins, 'const_s', $str, sval('Before thread'));
        op(@ins, 'say', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread, $code, $type);
        op(@ins, 'const_i64', $time, ival(500000));
        op(@ins, 'sleep', $time);
        op(@ins, 'const_s', $str, sval('In main thread at end'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "Before thread\nIn new thread\nLexical from outer\nIn main thread at end\n",
    "Thread can access outer lexicals");

mast_frame_output_is(-> $frame, @ins, $cu {
        sub thread_code() {
            my $frame := MAST::Frame.new();
            my $r0 := local($frame, str);
            my $r1 := local($frame, int);
            my @ins := $frame.instructions;
            op(@ins, 'const_s', $r0, sval('In new thread'));
            op(@ins, 'say', $r0);
            op(@ins, 'const_i64', $r1, ival(200000));
            op(@ins, 'sleep', $r1);
            op(@ins, 'const_s', $r0, sval('In new thread after sleep'));
            op(@ins, 'say', $r0);
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
        op(@ins, 'say', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread, $code, $type);
        op(@ins, 'jointhread', $thread);
        op(@ins, 'const_s', $str, sval('In main thread after join'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "Before thread\nIn new thread\nIn new thread after sleep\nIn main thread after join\n",
    "Can join a thread");

mast_frame_output_is(-> $frame, @ins, $cu {
        sub thread_code() {
            my $frame := MAST::Frame.new();
            my @ins := $frame.instructions;
            my $r0 := local($frame, str);
            op(@ins, 'const_s', $r0, sval('In new thread'));
            op(@ins, 'say', $r0);
            op(@ins, 'return');
            return $frame;
        }

        my $type := make_thread_type($frame);

        my $thread_code := thread_code();
        $cu.add_frame($thread_code);

        my $code    := local($frame, NQPMu);
        my $thread1 := local($frame, NQPMu);
        my $thread2 := local($frame, NQPMu);
        my $str     := local($frame, str);
        my $time    := local($frame, int);

        op(@ins, 'const_s', $str, sval('Before new threads'));
        op(@ins, 'say', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread1, $code, $type);
        op(@ins, 'newthread', $thread2, $code, $type);
        op(@ins, 'jointhread', $thread1);
        op(@ins, 'jointhread', $thread2);
        op(@ins, 'const_s', $str, sval('Joined both threads'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "Before new threads\nIn new thread\nIn new thread\nJoined both threads\n",
    "Can start and join multiple threads");

mast_frame_output_is(-> $frame, @ins, $cu {
        sub thread_code() {
            my $frame := MAST::Frame.new();
            my @ins := $frame.instructions;
            my $r0 := local($frame, int);
            my $r1 := local($frame, NQPMu);
            my $r2 := local($frame, str);
            my $l0 := label('loop');
            op(@ins, 'const_s', $r2, sval('In new thread'));
            op(@ins, 'say', $r2);
            op(@ins, 'const_i64', $r0, ival(1000000));
            nqp::push(@ins, $l0);
            op(@ins, 'knowhow', $r1);
            op(@ins, 'create', $r1, $r1);
            op(@ins, 'dec_i', $r0);
            op(@ins, 'if_i', $r0, $l0);
            op(@ins, 'return');
            return $frame;
        }

        my $type := make_thread_type($frame);

        my $thread_code := thread_code();
        $cu.add_frame($thread_code);

        my $code    := local($frame, NQPMu);
        my $thread1 := local($frame, NQPMu);
        my $thread2 := local($frame, NQPMu);
        my $thread3 := local($frame, NQPMu);
        my $thread4 := local($frame, NQPMu);
        my $str     := local($frame, str);
        my $time    := local($frame, int);

        op(@ins, 'const_s', $str, sval('Before new threads'));
        op(@ins, 'say', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread1, $code, $type);
        op(@ins, 'newthread', $thread2, $code, $type);
        op(@ins, 'newthread', $thread3, $code, $type);
        op(@ins, 'newthread', $thread4, $code, $type);
        op(@ins, 'jointhread', $thread1);
        op(@ins, 'jointhread', $thread2);
        op(@ins, 'jointhread', $thread3);
        op(@ins, 'jointhread', $thread4);
        op(@ins, 'const_s', $str, sval('Lived until after joins'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "Before new threads\nIn new thread\nIn new thread\nIn new thread\nIn new thread\nLived until after joins\n",
    "Multiple allocating threads work (threads + GC interaction)");


sub array_type($frame) {
    my @ins := $frame.instructions;
    my $r0 := local($frame, str);
    my $r1 := local($frame, NQPMu);
    my $r2 := local($frame, NQPMu);
    op(@ins, 'const_s', $r0, sval('VMArray'));
    op(@ins, 'knowhow', $r1);
    op(@ins, 'findmeth', $r2, $r1, sval('new_type'));
    call(@ins, $r2, [$Arg::obj, $Arg::named +| $Arg::str], $r1, sval('repr'), $r0, :result($r1));
    $r1
}

mast_frame_output_is(-> $frame, @ins, $cu {
        sub thread_code() {
            my $frame := MAST::Frame.new();
            my $at := array_type($frame);
            my @ins := $frame.instructions;
            my $r0 := local($frame, int);
            my $r1 := local($frame, NQPMu);
            my $r2 := local($frame, str);
            my $r3 := local($frame, NQPMu);
            my $l0 := label('loop');
            op(@ins, 'create', $r3, $at);
            op(@ins, 'const_s', $r2, sval('In new thread'));
            op(@ins, 'say', $r2);
            op(@ins, 'const_i64', $r0, ival(100000));
            nqp::push(@ins, $l0);
            op(@ins, 'create', $r1, $at);
            op(@ins, 'push_o', $r3, $r1);
            op(@ins, 'dec_i', $r0);
            op(@ins, 'if_i', $r0, $l0);
            op(@ins, 'return');
            return $frame;
        }

        my $type := make_thread_type($frame);

        my $thread_code := thread_code();
        $cu.add_frame($thread_code);

        my $code    := local($frame, NQPMu);
        my $thread1 := local($frame, NQPMu);
        my $thread2 := local($frame, NQPMu);
        my $thread3 := local($frame, NQPMu);
        my $thread4 := local($frame, NQPMu);
        my $str     := local($frame, str);
        my $time    := local($frame, int);

        op(@ins, 'const_s', $str, sval('Before new threads'));
        op(@ins, 'say', $str);
        op(@ins, 'getcode', $code, $thread_code);
        op(@ins, 'newthread', $thread1, $code, $type);
        op(@ins, 'newthread', $thread2, $code, $type);
        op(@ins, 'newthread', $thread3, $code, $type);
        op(@ins, 'newthread', $thread4, $code, $type);
        op(@ins, 'jointhread', $thread1);
        op(@ins, 'jointhread', $thread2);
        op(@ins, 'jointhread', $thread3);
        op(@ins, 'jointhread', $thread4);
        op(@ins, 'const_s', $str, sval('Lived until after joins'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "Before new threads\nIn new thread\nIn new thread\nIn new thread\nIn new thread\nLived until after joins\n",
    "Multiple allocating threads work (large heap usage)");

