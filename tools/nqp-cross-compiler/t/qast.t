#!nqp
use MASTTesting;

plan(5);

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

qast_output_is(QAST::Block.new(                   
    QAST::VM.new(
        moarop => 'say_i',
        QAST::VM.new(
            moarop => 'add_i',
            QAST::IVal.new( :value(42) ),
            QAST::IVal.new( :value(1) )
        )
    )
), "43\n", "expression result values");

qast_output_is(QAST::Block.new(
    QAST::VM.new(
        moarop => 'say_i',
        QAST::VM.new(
            moarop => 'add_i',
            QAST::IVal.new( :value(42) ),
            QAST::IVal.new( :value(1) )
        )
    ),
    QAST::VM.new(
        moarop => 'say_i',
        QAST::VM.new(
            moarop => 'add_i',
            QAST::IVal.new( :value(58) ),
            QAST::IVal.new( :value(7) )
        )
    )
), "43\n65\n", "expression result values reuse Locals");

#  Debug output for above test 5:
#  fresh elems: 0
#  fresh elems: 0
#  fresh elems: 0
#  release elems: 0
#  op add_i released arg result register with index: 0
#  release elems: 1
#  op add_i released arg result register with index: 1
#  release elems: 2
#  op say_i released arg result register with index: 2
#  fresh elems: 3
#  fresh elems: 2
#  fresh elems: 1
#  release elems: 0
#  op add_i released arg result register with index: 2
#  release elems: 1
#  op add_i released arg result register with index: 1
#  release elems: 2
#  op say_i released arg result register with index: 0
#  got 3 locals
#  processing local number 0
#  processing local number 1
#  processing local number 2
#  processing local number 0
#  processing local number 1
#  processing local number 2
#  processing local number 2
#  processing local number 1
#  processing local number 0
#  processing local number 2
#  processing local number 1
#  processing local number 0
#  output is: '43
#  65
#  '
#  expected is: '43
#  65
#  '
#  ok 5 - expression result values