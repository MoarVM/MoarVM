#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame {
        my $r0 := MAST::Local.new(:index($frame.add_local(str)));
        my @ins := $frame.instructions;
        nqp::push(@ins, MAST::Op.new(
                :bank('primitives'), :op('const_s'),
                $r0,
                MAST::SVal.new( :value('OMG strings!') )
            ));
        nqp::push(@ins, MAST::Op.new(
                :bank('dev'), :op('say_s'),
                $r0
            ));
        nqp::push(@ins, MAST::Op.new( :bank('primitives'), :op('return') ));
    },
    "OMG strings!\n",
    "string constant loading");
