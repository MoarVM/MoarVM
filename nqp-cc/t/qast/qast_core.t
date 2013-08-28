use MASTTesting;

plan(14);

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Stmts.new(
                    QAST::Op.new(
                        :op('say'),
                        QAST::IVal.new( :value(100) )
                    ),
                    QAST::IVal.new( :value(200) )
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
    "100\n200\n",
    "QAST::Stmts evalutes to the last value");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Stmt.new(
                    QAST::Op.new(
                        :op('say'),
                        QAST::SVal.new( :value('Yeti') )
                    ),
                    QAST::SVal.new( :value('Modus Hoperandi') )
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
    "Yeti\nModus Hoperandi\n",
    "QAST::Stmt evalutes to the last value");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('for'),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new(
                        :op('box_s'),
                        QAST::SVal.new( :value('Cucumber') ),
                        QAST::Op.new( :op('bootstr') )
                   ),
                   QAST::Op.new(
                        :op('box_s'),
                        QAST::SVal.new( :value('Hummus') ),
                        QAST::Op.new( :op('bootstr') )
                   )
                ),
                QAST::Block.new(
                    QAST::Op.new(
                        :op('say'),
                        QAST::Op.new(
                            :op('unbox_s'),
                            QAST::Var.new( :name('x'), :scope('local'), :decl('param') )
                        )
                    )
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
    "Cucumber\nHummus\n",
    "for");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('for'),
                QAST::Op.new(
                    :op('list'),
                    QAST::IVal.new( :value(1) ),
                    QAST::IVal.new( :value(2) ),
                    QAST::IVal.new( :value(3) ),
                    QAST::IVal.new( :value(4) ),
                    QAST::IVal.new( :value(5) )
                ),
                QAST::Block.new(
                    QAST::Var.new( :name('x'), :scope('local'), :decl('param'), :returns(int) ),
                    QAST::Op.new(
                        :op('if'),
                        QAST::Op.new(
                            :op('iseq_i'),
                            QAST::Var.new( :name('x'), :scope('local') ),
                            QAST::IVal.new( :value(2) )
                        ),
                        QAST::Op.new( :op('control'), :name('next') )
                    ),
                    QAST::Op.new(
                        :op('say'),
                        QAST::Var.new( :name('x'), :scope('local') )
                    ),
                    QAST::Op.new(
                        :op('if'),
                        QAST::Op.new(
                            :op('iseq_i'),
                            QAST::Var.new( :name('x'), :scope('local') ),
                            QAST::IVal.new( :value(4) )
                        ),
                        QAST::Op.new( :op('control'), :name('last') )
                    )
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
    "1\n3\n4\n",
    "for with control exceptions");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('$i'), :scope('lexical'), :decl('var'), :returns(int) ),
                QAST::IVal.new( :value(5) )
            ),
            QAST::Op.new(
                :op('while'),
                QAST::Var.new( :name('$i'), :scope('lexical') ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('$i'), :scope('lexical') )
                ),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('$i'), :scope('lexical') ),
                    QAST::Op.new(
                        :op('sub_i'),
                        QAST::Var.new( :name('$i'), :scope('lexical') ),
                        QAST::IVal.new( :value(1) )
                    )
                )),
                QAST::Op.new(
                    :op('say'),
                    QAST::SVal.new( :value('done') )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('$i'), :scope('lexical') )
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
    "5\n4\n3\n2\n1\ndone\n0\n",
    "while");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('$i'), :scope('lexical'), :decl('var'), :returns(int) ),
                QAST::IVal.new( :value(-1) )
            ),
            QAST::Op.new(
                :op('repeat_until'),
                QAST::Var.new( :name('$i'), :scope('lexical') ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('$i'), :scope('lexical') )
                ),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('$i'), :scope('lexical') ),
                    QAST::Op.new(
                        :op('add_i'),
                        QAST::Var.new( :name('$i'), :scope('lexical') ),
                        QAST::IVal.new( :value(1) )
                    )
                )),
                QAST::Op.new(
                    :op('say'),
                    QAST::SVal.new( :value('done') )
                ),
                QAST::Op.new(
                    :op('say'),
                    QAST::Var.new( :name('$i'), :scope('lexical') )
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
    "-1\n0\ndone\n1\n",
    "repeat_until");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('$i'), :scope('lexical'), :decl('var'), :returns(int) ),
                QAST::IVal.new( :value(5) )
            ),
            QAST::Op.new(
                :op('while'),
                QAST::Var.new( :name('$i'), :scope('lexical') ),
                QAST::Stmts.new(
                    QAST::Op.new(
                        :op('if'),
                        QAST::Op.new(
                            :op('iseq_i'),
                            QAST::Var.new( :name('$i'), :scope('lexical') ),
                            QAST::IVal.new( :value(4) )
                        ),
                        QAST::Op.new( :op('control'), :name('next') )
                    ),
                    QAST::Op.new(
                        :op('say'),
                        QAST::Var.new( :name('$i'), :scope('lexical') )
                    ),
                    QAST::Op.new(
                        :op('if'),
                        QAST::Op.new(
                            :op('iseq_i'),
                            QAST::Var.new( :name('$i'), :scope('lexical') ),
                            QAST::IVal.new( :value(2) )
                        ),
                        QAST::Op.new( :op('control'), :name('last') )
                    )),
                QAST::Op.new(
                    :op('bind'),
                    QAST::Var.new( :name('$i'), :scope('lexical') ),
                    QAST::Op.new(
                        :op('sub_i'),
                        QAST::Var.new( :name('$i'), :scope('lexical') ),
                        QAST::IVal.new( :value(1) )
                    )
                )),
                QAST::Op.new(
                    :op('say'),
                    QAST::SVal.new( :value('done') )
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
    "5\n3\n2\ndone\n",
    "while with control exceptionx");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('add_i'),
                    QAST::NVal.new( :value(4.1) ),
                    QAST::SVal.new( :value('3') )
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
    "7\n",
    "num and str coercion to int");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('add_n'),
                    QAST::IVal.new( :value(5) ),
                    QAST::SVal.new( :value('3.2') )
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
    "8.2\n",
    "int and str coercion to num");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('concat'),
                    QAST::IVal.new( :value(5) ),
                    QAST::NVal.new( :value(3.9) )
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
    "53.9\n",
    "int and num coercion to str");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('x'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::IVal.new( :value(100) ),
                    QAST::NVal.new( :value(3.3) ),
                    QAST::SVal.new( :value('Gangnam Style') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('unbox_i'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('x'), :scope('local') ),
                        QAST::IVal.new( :value(0) )
                    ))),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('unbox_n'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('x'), :scope('local') ),
                        QAST::IVal.new( :value(1) )
                    ))),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('unbox_s'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('x'), :scope('local') ),
                        QAST::IVal.new( :value(2) )
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
    "100\n3.3\nGangnam Style\n",
    "automatic boxing");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('x'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::IVal.new( :value(100) ),
                    QAST::NVal.new( :value(3.3) ),
                    QAST::SVal.new( :value('Gangnam Style') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('mul_i'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('x'), :scope('local') ),
                        QAST::IVal.new( :value(0) )
                    ),
                    QAST::IVal.new( :value(2) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('add_n'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('x'), :scope('local') ),
                        QAST::IVal.new( :value(1) )
                    ),
                    QAST::NVal.new( :value(5.5) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('concat'),
                    QAST::SVal.new( :value('Oppan ') ),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('x'), :scope('local') ),
                        QAST::IVal.new( :value(2) )
                    )
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
    "200\n8.8\nOppan Gangnam Style\n",
    "automatic boxing and unboxing");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('&x'), :scope('lexical'), :decl('var') ),
                QAST::Block.new(
                    QAST::Op.new(
                        :op('lexotic'), :name('RETURN'),
                        QAST::Op.new(
                            :op('call'),
                            QAST::Block.new(
                                QAST::Op.new(
                                    :op('call'),
                                    QAST::Var.new( :name('RETURN'), :scope('lexical') ),
                                    QAST::SVal.new( :value('badger') )
                                ),
                                QAST::SVal.new( :value('oops') )
                            )),
                        QAST::SVal.new( :value('oops') )
                    ))),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('unbox_s'),
                    QAST::Op.new( :op('call'), :name('&x') )
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
    "badger\n",
    "lexotic works");


qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('x'), :scope('local'), :decl('var') ),
                QAST::Block.new(
                    :name('potato')
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('getcodename'),
                    QAST::Var.new( :name('x'), :scope('local') )
                )),
            QAST::Op.new(
                :op('setcodename'),
                QAST::Var.new( :name('x'), :scope('local') ),
                QAST::SVal.new( :value('spud') )
            ),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('getcodename'),
                    QAST::Var.new( :name('x'), :scope('local') )
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
    "potato\nspud\n",
    "getcodename/setcodename");
