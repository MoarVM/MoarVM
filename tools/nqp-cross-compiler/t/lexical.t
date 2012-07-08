#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $foo := lexical($frame, int, '$foo');
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_i64', $r1, ival(45));
        op(@ins, 'bind_lex', $foo, $r1);
        op(@ins, 'get_lex', $r2, $foo);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "45",
    "bound and looked up a lexical in the current frame");
