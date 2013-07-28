#!nqp
use MASTTesting;

plan(4);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $foo := lexical($frame, int, '$foo');
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, str);
        op(@ins, 'const_i64', $r1, ival(45));
        op(@ins, 'bindlex', $foo, $r1);
        op(@ins, 'getlex', $r2, $foo);
        op(@ins, 'coerce_is', $r3, $r2);
        op(@ins, 'say', $r3);
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
            op(@ins, 'say', $r0);
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

mast_frame_output_is(-> $frame, @ins, $cu {
        sub counter_factory() {
            my $f_frame := MAST::Frame.new();
            $f_frame.set_outer($frame);

            sub counter_closure() {
                my $c_frame := MAST::Frame.new();
                $c_frame.set_outer($f_frame);
                my $r0 := local($c_frame, int);
                my @ins := $c_frame.instructions;
                op(@ins, 'getlex', $r0, MAST::Lexical.new( :index(0), :frames_out(1) ));
                op(@ins, 'inc_i', $r0);
                op(@ins, 'bindlex', MAST::Lexical.new( :index(0), :frames_out(1) ), $r0);
                op(@ins, 'return_i', $r0);
                return $c_frame;
            }

            my $c_frame := counter_closure();
            $cu.add_frame($c_frame);

            my @ins := $f_frame.instructions;
            my $count := lexical($f_frame, int, '$count');
            my $r0 := local($f_frame, int);
            my $r1 := local($f_frame, NQPMu);
            op(@ins, 'const_i64', $r0, ival(0));
            op(@ins, 'bindlex', $count, $r0);
            op(@ins, 'getcode', $r1, $c_frame);
            op(@ins, 'takeclosure', $r1, $r1);
            op(@ins, 'return_o', $r1);
            return $f_frame;
        }

        my $f_frame := counter_factory();
        $cu.add_frame($f_frame);

        my $c0 := local($frame, NQPMu);
        my $c1 := local($frame, NQPMu);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'getcode', $r0, $f_frame);
        nqp::push(@ins, MAST::Call.new(
                :target($r0),
                :flags([]),
                :result($c0)
            ));
        nqp::push(@ins, MAST::Call.new(
                :target($r0),
                :flags([]),
                :result($c1)
            ));
        nqp::push(@ins, MAST::Call.new(
                :target($c0),
                :flags([]),
                :result($r1)
            ));
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, MAST::Call.new(
                :target($c1),
                :flags([]),
                :result($r1)
            ));
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, MAST::Call.new(
                :target($c0),
                :flags([]),
                :result($r1)
            ));
        nqp::push(@ins, MAST::Call.new(
                :target($c0),
                :flags([]),
                :result($r1)
            ));
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        nqp::push(@ins, MAST::Call.new(
                :target($c1),
                :flags([]),
                :result($r1)
            ));
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "1\n1\n3\n2\n",
    "Closures work");

mast_frame_output_is(-> $frame, @ins, $cu {
        sub inner() {
            my $i_frame := MAST::Frame.new();
            $i_frame.set_outer($frame);
            my $r0 := local($i_frame, str);
            my @ins := $i_frame.instructions;
            op(@ins, 'getlex_ns', $r0, sval('$nom'));
            op(@ins, 'say', $r0);
            op(@ins, 'return');
            return $i_frame;
        }

        my $inner := inner();
        $cu.add_frame($inner);

        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, str);
        my $lex := lexical($frame, str, '$nom');
        op(@ins, 'const_s', $r1, sval('Bearnaisesås'));
        op(@ins, 'bindlex_ns', sval('$nom'), $r1);
        op(@ins, 'getcode', $r0, $inner);
        nqp::push(@ins, MAST::Call.new(
                :target($r0),
                :flags([])
            ));
        op(@ins, 'return');
    },
    "Bearnaisesås\n",
    "Lexical lookup and binding by name");
