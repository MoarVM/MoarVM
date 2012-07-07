#!nqp
use MASTTesting;

plan(10);

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

#  compiler.c debug output for above 5 tests:
#  c:\Users\mwilson\src\MoarVM\tools\nqp-cross-compiler>nqp t\qast.t
#  1..5
#  processing local number 0
#  processing local number 0
#  ok 1 - integer constant
#  processing local number 0
#  processing local number 0
#  ok 2 - float constant
#  processing local number 0
#  processing local number 0
#  ok 3 - string constant
#  processing local number 0
#  processing local number 1
#  processing local number 1
#  processing local number 0
#  processing local number 1
#  processing local number 1
#  ok 4 - expression result values
#  processing local number 0
#  processing local number 1
#  processing local number 1
#  processing local number 0
#  processing local number 1
#  processing local number 1
#  processing local number 1
#  processing local number 0
#  processing local number 0
#  processing local number 1
#  processing local number 0
#  processing local number 0
#  ok 5 - expression result values reuse Locals


qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(42) ),
        QAST::VM.new(
            moarop => 'say_i',
            QAST::IVal.new( :value(7) )),
        QAST::VM.new(
            moarop => 'say_i',
            QAST::IVal.new( :value(8) )))
), "7\n", "if/then/else true with else");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(42) ),
        QAST::Stmts.new(
            QAST::VM.new(
                moarop => 'say_i',
                QAST::IVal.new( :value(7) )),
            QAST::IVal.new( :value(50) )))
), "7\n", "if/then true");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(0) ),
        QAST::VM.new(
            moarop => 'say_i',
            QAST::IVal.new( :value(7) )),
        QAST::VM.new(
            moarop => 'say_i',
            QAST::IVal.new( :value(8) )))
), "8\n", "if/then/else false with else");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(0) ),
        QAST::Stmts.new(
            QAST::VM.new(
                moarop => 'say_i',
                QAST::IVal.new( :value(7) )),
            QAST::IVal.new( :value(50) )))
), "", "if/then false");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "foo", returns => str, decl => 'var', scope => 'local' ),
        QAST::SVal.new( :value("bar") )),
    QAST::VM.new(
        moarop => 'say_s',
        QAST::Var.new( name => "foo", scope => 'local' )),
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "baz", returns => num, decl => 'var', scope => 'local' ),
        QAST::NVal.new( :value(32.33) )),
    QAST::VM.new(
        moarop => 'say_n',
        QAST::Var.new( name => "baz", scope => 'local' ))
), "bar\n32.33\n", "local variable declaration, binding, saying");
