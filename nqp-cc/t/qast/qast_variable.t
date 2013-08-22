use MASTTesting;

plan(9);

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('msg'), :scope('local'), :decl('var'), :returns(str) ),
                    QAST::SVal.new( :value('Your friendly local...variable') )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('msg'), :scope('local') )
                )
            ));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Your friendly local...variable\n",
    "Local string variable");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('iv'), :scope('local'), :decl('var'), :returns(int) ),
                    QAST::IVal.new( :value(1001) )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('iv'), :scope('local') )
                ),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('iv'), :scope('local') ),
                    QAST::IVal.new( :value(2001) )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('iv'), :scope('local') )
                )
            ));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "1001\n2001\n",
    "Can re-bind locals to new values");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('$msg'), :scope('lexical'), :decl('var'), :returns(str) ),
                    QAST::SVal.new( :value('Forget Norway...Kenyaaaa!') )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('$msg'), :scope('lexical') )
                )
            ));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Forget Norway...Kenyaaaa!\n",
    "Lexical string variable in current scope");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('$msg'), :scope('lexical'), :decl('var'), :returns(str) ),
                    QAST::SVal.new( :value("The panda is the cucumber's enemy") )
                ),
                QAST::Op.new(
                    :op('call'), :returns(str),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Var.new( :name('$msg'), :scope('lexical') )
                        )))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "The panda is the cucumber's enemy\n",
    "Lexical string variable in outer scope");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('$msg'), :scope('lexical'), :decl('var'), :returns(str) ),
                    QAST::SVal.new( :value("Oops") )
                ),
                QAST::Op.new(
                    :op('call'), :returns(str),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('bind'),
                            QAST::Var.new( :name('$msg'), :scope('lexical') ),
                            QAST::SVal.new( :value("Everybody loves Magical Trevor") )
                        ))),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('$msg'), :scope('lexical') )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Everybody loves Magical Trevor\n",
    "Lexical string variable in outer scope");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&thunk'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('bind'),
                            QAST::Var.new( :name('$x'), :scope('lexical'), :decl('var'), :returns(str) ),
                            QAST::Var.new( :name('_x'), :scope('local'), :decl('param'), :returns(str) )
                        ),
                        QAST::Op.new(
                            :op('takeclosure'),
                            QAST::Block.new(
                                QAST::Var.new( :name('$x'), :scope('lexical') )
                            )
                        ))),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('a'), :scope('local'), :decl('var') ),
                    QAST::Op.new(
                        :op('call'), :name('&thunk'),
                        QAST::SVal.new( :value('Barley') )
                    )),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('b'), :scope('local'), :decl('var') ),
                    QAST::Op.new(
                        :op('call'), :name('&thunk'),
                        QAST::SVal.new( :value('Hops') )
                    )),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('call'), :returns(str),
                        QAST::Var.new( :name('a'), :scope('local') )
                    )),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('call'), :returns(str),
                        QAST::Var.new( :name('b'), :scope('local') )
                    ))));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Barley\nHops\n",
    "Basic closure test");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&a'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_s'),
                                QAST::Var.new( :name('$*x'), :scope('contextual') )
                            )),
                        QAST::Op.new(
                            :op('bind'),
                            QAST::Var.new( :name('$*x'), :scope('contextual') ),
                            QAST::SVal.new( :value("Then Drink Whisky") )
                        ))),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&b'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('bind'),
                            QAST::Var.new( :name('$*x'), :scope('lexical'), :decl('var') ),
                            QAST::SVal.new( :value("Burn After Reading") )
                        ),
                        QAST::Op.new( :op('call'), :name('&a') ),
                        QAST::Op.new(
                            :op('say'),
                            QAST::Op.new(
                                :op('unbox_s'),
                                QAST::Var.new( :name('$*x'), :scope('contextual') )
                            ))
                    )),
                QAST::Op.new( :op('call'), :name('&b') )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Burn After Reading\nThen Drink Whisky\n",
    "Dynamic lookup");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&x'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new( :op('call'), :name('&y') )
                    )),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('&y'), :scope('lexical'), :decl('var') ),
                    QAST::Block.new(
                        QAST::Op.new(
                            :op('say'),
                            QAST::SVal.new( :value('Bear + Deer = Beer') )
                        ))),
                QAST::Op.new( :op('call'), :name('&x') )
            ));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Bear + Deer = Beer\n",
    "Forward-declaration calls");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Stmts.new(
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('a'), :scope('local'), :decl('var') ),
                    QAST::SVal.new( :value('Zmrzlina') )
                ),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('b'), :scope('local'), :decl('var') ),
                    QAST::Op.new( :op('null') )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('unbox_s'),
                        QAST::VarWithFallback.new(
                            :name('a'), :scope('local'),
                            :fallback(QAST::SVal.new( :value('Chcem pivo') ))
                        ))),
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op('unbox_s'),
                        QAST::VarWithFallback.new(
                            :name('b'), :scope('local'),
                            :fallback(QAST::SVal.new( :value('Chcem pivo') ))
                        )))
            ));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Stmts.new(
                QAST::Var.new( :name('ARGS'), :scope('local'), :decl('param'), :slurpy(1) ),
                QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                ))))
    },
    "Zmrzlina\nChcem pivo\n",
    "QAST::VarWithFallback works");
