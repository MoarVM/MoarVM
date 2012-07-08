#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $foo := lexical($frame, int, '$foo');
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_i64', $r1, ival(45));
        op(@ins, 'bindlex', $foo, $r1);
        op(@ins, 'getlex', $r2, $foo);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "45\n",
    "bound and looked up a lexical in the current frame");

mast_frame_output_is(-> $frame, @ins, $cu {
        sub inner() {
            my $i_frame := MAST::Frame.new();
            $i_frame.set_outer($frame);
            my $r0 := local($i_frame, str);
            my @ins := $i_frame.instructions;
            op(@ins, 'getlex', $r0, MAST::Lexical.new( :index(0), :frames_out(1) ));
            op(@ins, 'say_s', $r0);
            op(@ins, 'return');
            return $i_frame;
        }
        
        my $inner := inner();
        $cu.add_frame($inner);
        
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, str);
        my $lex := lexical($frame, str, '$lex');
        op(@ins, 'const_s', $r1, sval('steak'));
        op(@ins, 'bindlex', $lex, $r1);
        op(@ins, 'getcode', $r0, $inner);
        nqp::push(@ins, MAST::Call.new(
                :target($r0),
                :flags([])
            ));
        op(@ins, 'return');
    },
    "steak\n",
    "Lookup of lexical in outer scope");
