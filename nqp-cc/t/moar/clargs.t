#!nqp
use MASTTesting;

plan(1);

qast_output_is(QAST::Block.new(
    QAST::Op.new( :op('bind'),
        QAST::Var.new( :name('args'), :scope('local'), :decl('var') ),
        QAST::VM.new( :moarop('clargs') )),
    QAST::VM.new( :moarop('say'),
        QAST::VM.new( :moarop('atpos_o'),
            QAST::Var.new( :name('args'), :scope('local') ),
            QAST::IVal.new( :value(1) ) ) )
), :clargs("foobar foobaz"), "foobaz\n", "grabbing the second clarg works");
