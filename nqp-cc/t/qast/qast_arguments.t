use MASTTesting;

plan(13);

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&say'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('a_param'), :scope('local'), :decl('param'), :returns(str) )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&say'), :returns(str),
                    QAST::SVal.new( :value("Ailse 2! That's the place where we sell the Ragu!") )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "Ailse 2! That's the place where we sell the Ragu!\n",
    "Passing a string argument");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&greet'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new(
                                :name('a_param'), :scope('local'), :decl('param'), :returns(str),
                                :default(QAST::SVal.new( :value('Hello') )) )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&greet'), :returns(str),
                    QAST::SVal.new( :value("Dobry den") )
                ),
                QAST::Op.new(
                    :op('call'), :name('&greet'), :returns(str)
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "Dobry den\nHello\n",
    "Optional string argument");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&add'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('add_i'),
                            QAST::Var.new( :name('a_param'), :scope('local'), :decl('param'), :returns(int) ),
                            QAST::Var.new( :name('b_param'), :scope('local'), :decl('param'), :returns(int) )
                        ))),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('call'), :name('&add'), :returns(int),
                        QAST::IVal.new( :value(39) ),
                        QAST::IVal.new( :value(3) )
                    ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "42\n",
    "Integer arguments and return value");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&add'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('add_i'),
                            QAST::Var.new( :name('a_param'), :scope('lexical'), :decl('param'), :returns(int) ),
                            QAST::Var.new( :name('b_param'), :scope('lexical'), :decl('param'), :returns(int) )
                        ))),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('call'), :name('&add'), :returns(int),
                        QAST::IVal.new( :value(39) ),
                        QAST::IVal.new( :value(3) )
                    ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "42\n",
    "Integer arguments and return value (lexical)");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&add'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('div_i'),
                            QAST::Var.new( :name('a_param'), :named('a'), :scope('local'), :decl('param'), :returns(int) ),
                            QAST::Var.new( :name('b_param'), :named('b'), :scope('local'), :decl('param'), :returns(int) )
                        ))),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('call'), :name('&add'), :returns(int),
                        QAST::IVal.new( :value(3), :named('b') ),
                        QAST::IVal.new( :value(27), :named('a') )
                    ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "9\n",
    "Integer named arguments");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&greet'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new(
                                :name('a_param'), :named('greeting'), :scope('local'), :decl('param'), :returns(str),
                                :default(QAST::SVal.new( :value('Hi') )) )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&greet'), :returns(str),
                    QAST::SVal.new( :value("Hola"), :named('greeting') )
                ),
                QAST::Op.new(
                    :op('call'), :name('&greet'), :returns(str)
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "Hola\nHi\n",
    "Optional string named parameter");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&slurp_em'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Var.new( :name('s'), :scope('local'), :decl('param'), :slurpy(1) ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('elems'),
                                QAST::Var.new( :name('s'), :scope('local') )
                            )),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_i'),
                                QAST::Op.new(
                                    :op('atpos'),
                                    QAST::Var.new( :name('s'), :scope('local') ),
                                    QAST::IVal.new( :value(0) )
                                ))),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_n'),
                                QAST::Op.new(
                                    :op('atpos'),
                                    QAST::Var.new( :name('s'), :scope('local') ),
                                    QAST::IVal.new( :value(1) )
                                ))),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_s'),
                                QAST::Op.new(
                                    :op('atpos'),
                                    QAST::Var.new( :name('s'), :scope('local') ),
                                    QAST::IVal.new( :value(2) )
                                )))
                    )),
                QAST::Op.new(
                    :op('call'), :name('&slurp_em'), :returns(str),
                    QAST::IVal.new( :value(5) ),
                    QAST::NVal.new( :value(9.8) ),
                    QAST::SVal.new( :value('FREE BROWNIES') )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "3\n5\n9.8\nFREE BROWNIES\n",
    "Slurpy positional parameters");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&slurp_em'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Var.new( :name('s'), :scope('local'), :decl('param'), :slurpy(1), :named(1) ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('elems'),
                                QAST::Var.new( :name('s'), :scope('local') )
                            )),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_i'),
                                QAST::Op.new(
                                    :op('atkey'),
                                    QAST::Var.new( :name('s'), :scope('local') ),
                                    QAST::SVal.new( :value('i') )
                                ))),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_n'),
                                QAST::Op.new(
                                    :op('atkey'),
                                    QAST::Var.new( :name('s'), :scope('local') ),
                                    QAST::SVal.new( :value('n') )
                                ))),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_s'),
                                QAST::Op.new(
                                    :op('atkey'),
                                    QAST::Var.new( :name('s'), :scope('local') ),
                                    QAST::SVal.new( :value('s') )
                                )))
                    )),
                QAST::Op.new(
                    :op('call'), :name('&slurp_em'), :returns(str),
                    QAST::IVal.new( :value(5), :named('i') ),
                    QAST::NVal.new( :value(6.5), :named('n') ),
                    QAST::SVal.new( :value('Venison'), :named('s') )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "3\n5\n6.5\nVenison\n",
    "Slurpy named parameters");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&many_things'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('a'), :scope('local'), :decl('param'), :returns(int) )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('b'), :scope('local'), :decl('param'), :returns(num) )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('c'), :scope('local'), :decl('param'), :returns(str) )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&many_things'),
                    QAST::Op.new(
                        :op('list'), :flat(1),
                        QAST::IVal.new( :value(25) ),
                        QAST::NVal.new( :value(6.9) ),
                        QAST::SVal.new( :value('Karai') )
                    ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "25\n6.9\nKarai\n",
    "Flattening positional arguments");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&many_things'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('a'), :scope('local'), :decl('param'), :returns(int) )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('b'), :scope('local'), :decl('param'), :returns(num) )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('c'), :scope('local'), :decl('param'), :returns(str) )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&many_things'),
                    QAST::IVal.new( :value(25) ),
                    QAST::Op.new(
                        :op('list'), :flat(1),
                        QAST::NVal.new( :value(6.9) )
                    ),
                    QAST::SVal.new( :value('Karai') ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "25\n6.9\nKarai\n",
    "Flattening positional arguments in middle of other args");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&many_named_things'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('a'), :scope('local'), :decl('param'), :returns(int), :named('a') )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('b'), :scope('local'), :decl('param'), :returns(num), :named('b') )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('c'), :scope('local'), :decl('param'), :returns(str), :named('c') )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&many_named_things'),
                    QAST::Op.new(
                        :op('hash'), :flat(1), :named(1),
                        QAST::SVal.new( :value('c') ),
                        QAST::SVal.new( :value('Byriani') ),
                        QAST::SVal.new( :value('a') ),
                        QAST::IVal.new( :value(42) ),
                        QAST::SVal.new( :value('b') ),
                        QAST::NVal.new( :value(4.2) )
                    ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "42\n4.2\nByriani\n",
    "Flattening named arguments");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&many_named_things'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('a'), :scope('local'), :decl('param'), :returns(int), :named('a') )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('b'), :scope('local'), :decl('param'), :returns(num), :named('b') )
                        ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('c'), :scope('local'), :decl('param'), :returns(str), :named('c') )
                        ))),
                QAST::Op.new(
                    :op('call'), :name('&many_named_things'),
                    QAST::SVal.new( :value('Byriani'), :named('c') ),
                    QAST::Op.new(
                        :op('hash'), :flat(1), :named(1),
                        QAST::SVal.new( :value('a') ),
                        QAST::IVal.new( :value(42) ),
                    ),
                    QAST::NVal.new( :value(4.2), :named('b') ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "42\n4.2\nByriani\n",
    "Flattening named arguments within other args");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&boxer'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_i'),
                                QAST::Var.new( :name('a'), :scope('local'), :decl('param') )
                            )),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_n'),
                                QAST::Var.new( :name('b'), :scope('local'), :decl('param') )
                            )),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_s'),
                                QAST::Var.new( :name('c'), :scope('local'), :decl('param') )
                            )))),
                QAST::Op.new(
                    :op('call'), :name('&boxer'),
                    QAST::IVal.new( :value(101) ),
                    QAST::NVal.new( :value(2.7) ),
                    QAST::SVal.new( :value('Pathia') )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "101\n2.7\nPathia\n",
    "Auto-boxing of arguments");
