#!nqp
use MASTTesting;

plan(5);

mast_frame_output_is(-> $frame, @ins {
        my $r0 := MAST::Local.new(:index($frame.add_local(num)));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(233.232) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "233.232000\n",
    "float constant loading");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := MAST::Local.new(:index($frame.add_local(num)));
        my $r1 := MAST::Local.new(:index($frame.add_local(num)));
        my $r2 := MAST::Local.new(:index($frame.add_local(num)));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(-34222.10004) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r1,
                MAST::NVal.new( :value(9993292.1123) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('add_n'),
                $r2, $r0, $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r2
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "9959070.012260\n",
    "float addition");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := MAST::Local.new(:index($frame.add_local(num)));
        my $r1 := MAST::Local.new(:index($frame.add_local(num)));
        my $r2 := MAST::Local.new(:index($frame.add_local(num)));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(3838890000.223) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r1,
                MAST::NVal.new( :value(332424432.22222) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('sub_n'),
                $r2, $r0, $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r2
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "3506465568.000780\n",
    "float subtraction");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := MAST::Local.new(:index($frame.add_local(num)));
        my $r1 := MAST::Local.new(:index($frame.add_local(num)));
        my $r2 := MAST::Local.new(:index($frame.add_local(num)));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(-332233.22333) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r1,
                MAST::NVal.new( :value(382993.23) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('mul_n'),
                $r2, $r0, $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r2
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "-127243075316.468050\n",
    "float multiplication");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := MAST::Local.new(:index($frame.add_local(num)));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(-38838.000033332) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('neg_n'),
                $r0,
                $r0
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(33223.22003374) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('neg_n'),
                $r0,
                $r0
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "38838.000033\n-33223.220034\n",
    "float negation");
