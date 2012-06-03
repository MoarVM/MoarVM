#!nqp
use MASTTesting;

plan(5);

mast_frame_output_is(-> $frame {
        my $r0 := MAST::Local.new(:index($frame.add_local(int)));
        my $l1 := MAST::Label.new(:name('foo'));
        my @ins := $frame.instructions;
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(1) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('goto'),
                $l1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(0) )
            ));
        nqp::push(@ins, $l1);
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_i'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "1\n",
    "unconditional forward branching");

mast_frame_output_is(-> $frame {
        my $r0 := MAST::Local.new(:index($frame.add_local(int)));
        my $l1 := MAST::Label.new(:name('foo'));
        my $l2 := MAST::Label.new(:name('bar'));
        my @ins := $frame.instructions;
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(1) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('goto'),
                $l1
            ));
        # Next instruction deliberately unreachable.
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(3) )
            ));
        nqp::push(@ins, $l2);
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_i'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
        nqp::push(@ins, $l1);
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(2) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('goto'),
                $l2
            ));
    },
    "2\n",
    "unconditional forward and backward branching");

mast_frame_output_is(-> $frame {
        my $r0 := MAST::Local.new(:index($frame.add_local(int)));
        my $r1 := MAST::Local.new(:index($frame.add_local(int)));
        my $loop := MAST::Label.new(:name('loop'));
        my $loop_end := MAST::Label.new(:name('loop_end'));
        my @ins := $frame.instructions;
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(5) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r1,
                MAST::IVal.new( :value(0) )
            ));
        nqp::push(@ins, $loop);
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('unless_i'),
                $r0, $loop_end
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('inc_i'),
                $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('dec_i'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_i'),
                $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('goto'),
                $loop
            ));
        nqp::push(@ins, $loop_end);
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "1\n2\n3\n4\n5\n",
    "conditional on zero integer branching");

mast_frame_output_is(-> $frame {
        my $r0 := MAST::Local.new(:index($frame.add_local(int)));
        my $r1 := MAST::Local.new(:index($frame.add_local(int)));
        my $loop := MAST::Label.new(:name('loop'));
        my @ins := $frame.instructions;
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r0,
                MAST::IVal.new( :value(3) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $r1,
                MAST::IVal.new( :value(0) )
            ));
        nqp::push(@ins, $loop);
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('inc_i'),
                $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('dec_i'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_i'),
                $r1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('if_i'),
                $r0, $loop
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "1\n2\n3\n",
    "conditional on non-zero integer branching");

mast_frame_output_is(-> $frame {
        my $r0 := MAST::Local.new(:index($frame.add_local(num)));
        my $r1 := MAST::Local.new(:index($frame.add_local(num)));
        my $l0 := MAST::Label.new(:name('l0'));
        my $l1 := MAST::Label.new(:name('l1'));
        my $l2 := MAST::Label.new(:name('l2'));
        my $l3 := MAST::Label.new(:name('l3'));
        my @ins := $frame.instructions;
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r0,
                MAST::NVal.new( :value(0.0) )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $r1,
                MAST::NVal.new( :value(1.0) )
            ));
        # unless 0 don't say 1
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('unless_n'),
                $r0, $l0
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r1
            ));
        nqp::push(@ins, $l0);
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        # unless 1 don't say 1
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('unless_n'),
                $r1, $l1
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r1
            ));
        nqp::push(@ins, $l1);
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        # if 0 don't say 1
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('if_n'),
                $r0, $l2
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r1
            ));
        nqp::push(@ins, $l2);
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        # if 1 don't say 1
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('if_n'),
                $r1, $l3
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r1
            ));
        nqp::push(@ins, $l3);
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_n'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "0.000000\n1.000000\n0.000000\n1.000000\n0.000000\n0.000000\n",
    "conditional on zero and non-zero float branching");