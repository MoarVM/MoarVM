#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        sub make_annotated() {
            my @ins := nqp::list;
            op(@ins, 'say_s', const($frame, sval("Alive")));
            @ins
        }
        nqp::push(@ins, annotated(make_annotated(), "file1", 12));
        nqp::push(@ins, annotated(make_annotated(), "file2", 13));
    },
    "Alive\nAlive\n",
    "annotation survives");
