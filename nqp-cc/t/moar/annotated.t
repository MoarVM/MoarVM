#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        sub make_annotated() {
            my @ins := nqp::list;
            my $r1 := local($frame, str);
            op(@ins, 'const_s', $r1, sval('Alive'));
            op(@ins, 'say_s', $r1);
            @ins
        }
        op(@ins, 'goto', label("foo"));
        nqp::push(@ins, label("foo"));
        nqp::push(@ins, annotated(make_annotated(), "file1", 12));
        nqp::push(@ins, annotated(make_annotated(), "file2", 13));
    },
    "Alive\nAlive\n",
    "annotation survives");
