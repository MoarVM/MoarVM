#!nqp
use MASTTesting;

plan(3);

qast_output_is(QAST::Block.new(
    QAST::VM.new(
        moarop => 'say_i',
        QAST::IVal.new( :value(42) )
    )
), "42", "integer constant");

qast_output_is(QAST::Block.new(
    QAST::VM.new(
        moarop => 'say_n',
        QAST::NVal.new( :value(56.003) )
    )
), "56.003", "float constant");

qast_output_is(QAST::Block.new(
    QAST::VM.new(
        moarop => 'say_s',
        QAST::SVal.new( :value("howdyhowdy") )
    )
), "howdyhowdy\n", "string constant");
