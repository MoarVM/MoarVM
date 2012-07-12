#!nqp
use MASTTesting;

plan(28);

qast_output_is(QAST::Block.new(
    QAST::VM.new( moarop => 'say_i',
        QAST::IVal.new( :value(42) )
    )
), "42\n", "integer constant");

qast_output_is(QAST::Block.new(
    QAST::VM.new( moarop => 'say_n',
        QAST::NVal.new( :value(56.003) )
    )
), "56.003\n", "float constant", approx => 1);

qast_output_is(QAST::Block.new(
    QAST::VM.new( moarop => 'say_s',
        QAST::SVal.new( :value("howdyhowdy") )
    )
), "howdyhowdy\n", "string constant");

qast_output_is(QAST::Block.new(                   
    QAST::VM.new( moarop => 'say_i',
        QAST::VM.new( moarop => 'add_i',
            QAST::IVal.new( :value(42) ),
            QAST::IVal.new( :value(1) )
        )
    )
), "43\n", "expression result values");

qast_output_is(QAST::Block.new(
    QAST::VM.new( moarop => 'say_i',
        QAST::VM.new( moarop => 'add_i',
            QAST::IVal.new( :value(42) ),
            QAST::IVal.new( :value(1) )
        )
    ),
    QAST::VM.new( moarop => 'say_i',
        QAST::VM.new( moarop => 'add_i',
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
        QAST::VM.new( moarop => 'say_i',
            QAST::IVal.new( :value(7) )),
        QAST::VM.new( moarop => 'say_i',
            QAST::IVal.new( :value(8) )))
), "7\n", "if then else true with else");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(42) ),
        QAST::Stmts.new(
            QAST::VM.new( moarop => 'say_i',
                QAST::IVal.new( :value(7) )),
            QAST::IVal.new( :value(50) )))
), "7\n", "if then true");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(0) ),
        QAST::VM.new( moarop => 'say_i',
            QAST::IVal.new( :value(7) )),
        QAST::VM.new( moarop => 'say_i',
            QAST::IVal.new( :value(8) )))
), "8\n", "if then else false with else");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'if',
        QAST::IVal.new( :value(0) ),
        QAST::Stmts.new(
            QAST::VM.new( moarop => 'say_i',
                QAST::IVal.new( :value(7) )),
            QAST::IVal.new( :value(50) )))
), "", "if then false");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "foo", returns => str, decl => 'var', scope => 'local' ),
        QAST::SVal.new( :value("bar") )),
    QAST::VM.new( moarop => 'say_s',
        QAST::Var.new( name => "foo", scope => 'local' ))
), "bar\n", "local variable declaration, binding, saying");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "foo", returns => int, decl => 'var', scope => 'local' ),
        QAST::IVal.new( :value(4) )),
    QAST::Op.new( op=> 'while',
        QAST::Var.new( name => "foo", scope => 'local' ),
        QAST::Stmts.new(
            QAST::VM.new( moarop => 'say_i',
                QAST::Var.new( name => "foo", scope => 'local' )),
            QAST::VM.new( moarop => 'dec_i',
                QAST::Var.new( name => "foo", scope => 'local' ))))
), "4\n3\n2\n1\n", "while loop and decrementing local var");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "foo", returns => int, decl => 'var', scope => 'local' ),
        QAST::IVal.new( :value(4) )),
    QAST::Op.new( op=> 'until',
        QAST::VM.new( moarop => 'eq_i',
            QAST::Var.new( name => "foo", scope => 'local' ),
            QAST::IVal.new( :value(0) )),
        QAST::Stmts.new(
            QAST::VM.new( moarop => 'say_i',
                QAST::Var.new( name => "foo", scope => 'local' )),
            QAST::VM.new( moarop => 'dec_i',
                QAST::Var.new( name => "foo", scope => 'local' ))))
), "4\n3\n2\n1\n", "until loop and decrementing local var");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "foo", returns => int, decl => 'var', scope => 'local' ),
        QAST::IVal.new( :value(4) )),
    QAST::Op.new( op=> 'repeat_while',
        QAST::Stmts.new(
            QAST::VM.new( moarop => 'say_i',
                QAST::Var.new( name => "foo", scope => 'local' )),
            QAST::VM.new( moarop => 'dec_i',
                QAST::Var.new( name => "foo", scope => 'local' ))),
        QAST::Var.new( name => "foo", scope => 'local' ))
), "4\n3\n2\n1\n", "repeat_while loop and decrementing local var");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'bind',
        QAST::Var.new( name => "foo", returns => int, decl => 'var', scope => 'local' ),
        QAST::IVal.new( :value(4) )),
    QAST::Op.new( op=> 'repeat_until',
        QAST::Stmts.new(
            QAST::VM.new( moarop => 'say_i',
                QAST::Var.new( name => "foo", scope => 'local' )),
            QAST::VM.new( moarop => 'dec_i',
                QAST::Var.new( name => "foo", scope => 'local' ))),
        QAST::VM.new( moarop => 'eq_i',
            QAST::Var.new( name => "foo", scope => 'local' ),
            QAST::IVal.new( :value(0) )))
), "4\n3\n2\n1\n", "repeat_until loop and decrementing local var");

my $block := QAST::Block.new( QAST::IVal.new( :value(666) ) );
qast_output_is(QAST::Block.new(
    $block,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block) )
        )
    )),
    "666\n",
    'BVal node');

my $block2 := QAST::Block.new(
    QAST::Var.new( :name('a'), :scope('local'), :decl('param'), :returns(int) ),
    QAST::Var.new( :name('b'), :scope('local'), :decl('param'), :returns(int) ),
    QAST::Var.new( :name('c'), :scope('local'), :decl('param'), :returns(int) ),
    QAST::Var.new( :name('d'), :scope('local'), :decl('param'), :returns(str) ),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('a'), :scope('local') ) ),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('b'), :scope('local') ) ),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('c'), :scope('local') ) ),
    QAST::VM.new( :moarop('say_s'),
        QAST::Var.new( :name('d'), :scope('local') ) ),
    QAST::IVal.new( :value(0) )
);
qast_output_is(QAST::Block.new(
    $block2,
    QAST::Op.new( :op('call'), :returns(int),
        QAST::BVal.new( :value($block2) ),
        QAST::IVal.new( :value(777) ),
        QAST::IVal.new( :value(888) ),
        QAST::IVal.new( :value(999) ),
        QAST::SVal.new( :value("hi") )
    )
), "777\n888\n999\nhi\n", 'four positional required local args and params work');

my $block4 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('local'), :decl('param'), :returns(int) ),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('local') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block4,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block4) ),
            QAST::IVal.new( :named("foo"), :value(777) ) ) )
), "777\n888\n", 'one required named local arg and param works');

my $block5 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('local'), :decl('param'), :returns(int),
        :default(QAST::Stmts.new(
            QAST::VM.new( :moarop('say_s'),
                QAST::SVal.new( :value("in init code"))),
            QAST::IVal.new( :value(444) )
        ))),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('local') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block5,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block5) ),
            QAST::IVal.new( :named("foo"), :value(777) ) ) )
), "777\n888\n", 'one optional named local arg and param works');

my $block6 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('local'), :decl('param'), :returns(int),
        :default(QAST::Stmts.new(
            QAST::VM.new( :moarop('say_s'),
                QAST::SVal.new( :value("in init code"))),
            QAST::IVal.new( :value(444) )
        ))),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('local') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block6,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block6) ) ) )
), "in init code\n444\n888\n", 'one optional named local param without an arg works');

############

my $block14 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('lexical'), :decl('param'), :returns(int) ),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('lexical') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block14,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block14) ),
            QAST::IVal.new( :named("foo"), :value(777) ) ) )
), "777\n888\n", 'one required named lexical arg and param works');

my $block15 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('lexical'), :decl('param'), :returns(int),
        :default(QAST::Stmts.new(
            QAST::VM.new( :moarop('say_s'),
                QAST::SVal.new( :value("in init code"))),
            QAST::IVal.new( :value(444) )
        ))),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('lexical') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block15,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block15) ),
            QAST::IVal.new( :named("foo"), :value(777) ) ) )
), "777\n888\n", 'one optional named lexical arg and param works');

my $block16 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('lexical'), :decl('param'), :returns(int),
        :default(QAST::Stmts.new(
            QAST::VM.new( :moarop('say_s'),
                QAST::SVal.new( :value("in init code"))),
            QAST::IVal.new( :value(444) )
        ))),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('lexical') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block16,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block16) ) ) )
), "in init code\n444\n888\n", 'one optional named lexical param without an arg works');

my $block17 := QAST::Block.new(
    QAST::VM.new( :moarop('say_s'),
        QAST::Var.new( :name("foo"), :scope('lexical') ) ),
    QAST::IVal.new( :value(0) )
);

qast_output_is(QAST::Block.new(
    QAST::Op.new( :op('bind'),
        QAST::Var.new( :name("foo"), :scope('lexical'), :decl('var'), :returns(str) ),
        QAST::SVal.new( :value("bar") ) ),
    $block17,
    QAST::Op.new( :op('call'), :returns(int),
        QAST::BVal.new( :value($block17) ) )
), "bar\n", 'lexical one level up works');

my $block18 := QAST::Block.new(
    QAST::Var.new( :named("foo"), :name("foo"), :scope('lexical'), :decl('param'), :returns(int),
        :default(QAST::Stmts.new(
            QAST::VM.new( :moarop('say_s'),
                QAST::SVal.new( :value("in init code"))),
            QAST::IVal.new( :value(444) )
        ))),
    QAST::VM.new( :moarop('say_i'),
        QAST::Var.new( :name('foo'), :scope('lexical') ) ),
    QAST::VM.new( :moarop('return_i'),
        QAST::IVal.new( :value(888) ) ) );

qast_output_is(QAST::Block.new(
    $block18,
    QAST::VM.new( :moarop('say_i'),
        QAST::Op.new( :op('call'), :returns(int),
            QAST::BVal.new( :value($block18) ),
            QAST::IVal.new( :named("foo"), :value(0) ) ) )
), "0\n888\n", 'zero-valued optional arg works');

qast_output_is(QAST::Block.new(
    QAST::Op.new( :op('bind'),
        QAST::Var.new( :name('knowhow'), :scope('local'), :decl('var') ),
        QAST::VM.new( :moarop('knowhow') )),
    QAST::Op.new( :op('bind'),
        QAST::Var.new( :name('how'), :scope('local'), :decl('var') ),
        QAST::VM.new( :moarop('gethow'),
            QAST::Var.new( :name('knowhow'), :scope('local'))) ),
    QAST::VM.new( :moarop('say_s'),
        QAST::Op.new( :op('callmethod'), :returns(str), :name('name'),
            QAST::Var.new( :name('how'), :scope('local') ),
            QAST::Var.new( :name('knowhow'), :scope('local') ) ) )
), "KnowHOW\n", "method call with zero args works");

qast_output_is(QAST::Block.new(
    QAST::Op.new( :op('bind'),
        QAST::Var.new( :name('knowhow'), :scope('local'), :decl('var') ),
        QAST::VM.new( :moarop('knowhow') )),
    QAST::Op.new( :op('bind'),
        QAST::Var.new( :name('how'), :scope('local'), :decl('var') ),
        QAST::VM.new( :moarop('gethow'),
            QAST::Var.new( :name('knowhow'), :scope('local'))) ),
    QAST::VM.new( :moarop('say_s'),
        QAST::Op.new( :op('callmethod'), :returns(str),
            QAST::Var.new( :name('how'), :scope('local') ),
            QAST::VM.new( :moarop('concat_s'),
                QAST::SVal.new( :value('na') ),
                QAST::SVal.new( :value('me') ) ),
            QAST::Var.new( :name('knowhow'), :scope('local') ) ) )
), "KnowHOW\n", "method call by string name with zero args works");

qast_output_is(QAST::Block.new(
    QAST::VM.new( :moarop('say_i'),
        QAST::Stmt.new( :resultchild(1),
            QAST::IVal.new( :value(3) ),
            QAST::IVal.new( :value(4) ),
            QAST::IVal.new( :value(5) ) ) ),
    QAST::VM.new( :moarop('say_i'),
        QAST::Stmts.new( :resultchild(0),
            QAST::IVal.new( :value(1) ),
            QAST::IVal.new( :value(2) ) ) )
), "4\n1\n", "resultchild works with QAST Stmt and Stmts");

qast_output_is(
    QAST::Block.new(
        QAST::Op.new(
            :op('bind'),
            QAST::Var.new( :name('$x'), :scope('lexical'), :decl('var'), :returns(int) ),
            QAST::IVal.new( :value(444) )
        ),
        QAST::Block.new(
            :blocktype('immediate'),
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('$x'), :scope('lexical') ),
                QAST::IVal.new( :value(555) )
            )
        ),
        QAST::VM.new(
            :moarop('say_i'),
            QAST::Var.new( :name('$x'), :scope('lexical') )
        )
    ),
    "555\n",
    'lexical binding in a nested block');
