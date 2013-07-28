use MASTTesting;

plan(14);

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') )
                    )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "0\n2\n",
    "Can create empty/2-elem list and get elems");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('l'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') )
                    )
                )
            ),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('l'), :scope('local') ),
                        QAST::IVal.new( :value(0) )
                    )
                )),
            QAST::Op.new(
                :op('bindpos'),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::IVal.new( :value(1) ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('l'), :scope('local') ),
                        QAST::IVal.new( :value(0) )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('atpos'),
                        QAST::Var.new( :name('l'), :scope('local') ),
                        QAST::IVal.new( :value(1) )
                    )
                )),
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n3\n2\n3\n2\n",
    "Basic atpos and bindpos usage");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new( :op('hash') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('hash'),
                        QAST::SVal.new( :value('whisky') ),
                        QAST::Op.new( :op('knowhow') ),
                        QAST::SVal.new( :value('vodka') ),
                        QAST::Op.new( :op('knowhow') )
                    )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "0\n2\n",
    "Can create empty/2-elem hash and get elems");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('h'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('hash'),
                    QAST::SVal.new( :value('whisky') ),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') )
                    ),
                    QAST::SVal.new( :value('vodka') ),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('atkey'),
                        QAST::Var.new( :name('h'), :scope('local') ),
                        QAST::SVal.new( :value('vodka') )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('atkey'),
                        QAST::Var.new( :name('h'), :scope('local') ),
                        QAST::SVal.new( :value('whisky') )
                    )
                )),
            QAST::Op.new(
                :op('bindkey'),
                QAST::Var.new( :name('h'), :scope('local') ),
                QAST::SVal.new( :value('whisky') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Op.new(
                        :op('atkey'),
                        QAST::Var.new( :name('h'), :scope('local') ),
                        QAST::SVal.new( :value('whisky') )
                    )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n2\n3\n",
    "Basic atkey and bindkey usage");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('h'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('hash'),
                    QAST::SVal.new( :value('whisky') ),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') )
                    ),
                    QAST::SVal.new( :value('vodka') ),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('existskey'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('vodka') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('existskey'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('whisky') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('existskey'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('beer') )
                )),
            QAST::Op.new(
                :op('deletekey'),
                QAST::Var.new( :name('h'), :scope('local') ),
                QAST::SVal.new( :value('whisky') )
            ),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('existskey'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('vodka') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('existskey'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('whisky') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('existskey'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('beer') )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n1\n0\n1\n0\n0\n",
    "existskey and deletekey");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('islist'),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('islist'),
                    QAST::Op.new( :op('hash') )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n0\n",
    "islist");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('ishash'),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('ishash'),
                    QAST::Op.new( :op('hash') )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "0\n1\n",
    "ishash");

qast_test(
    -> {
        my $block := QAST::Block.new(
            # Get iterator.
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('it'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('iterator'),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new(
                            :op('box_s'),
                            QAST::SVal.new( :value('Curry') ),
                            QAST::Op.new( :op('bootstr') )
                       ),
                       QAST::Op.new(
                            :op('box_s'),
                            QAST::SVal.new( :value('Beer') ),
                            QAST::Op.new( :op('bootstr') )
                       )
                    ))),

            # Use it to get values.
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('istrue'),
                    QAST::Var.new( :name('it'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('unbox_s'),
                    QAST::Op.new(
                        :op('shift'),
                        QAST::Var.new( :name('it'), :scope('local') )
                    ))),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('istrue'),
                    QAST::Var.new( :name('it'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('unbox_s'),
                    QAST::Op.new(
                        :op('shift'),
                        QAST::Var.new( :name('it'), :scope('local') )
                    ))),

            # Check it's now empty (false).
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('istrue'),
                    QAST::Var.new( :name('it'), :scope('local') )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\nCurry\n1\nBeer\n0\n",
    "Array iteration");

qast_test(
    -> {
        my $block := QAST::Block.new(
            # Get iterator for a hash.
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('it'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('iterator'),
                    QAST::Op.new(
                        :op('hash'),
                        QAST::SVal.new( :value('Noah') ),
                        QAST::Op.new(
                            :op('box_i'),
                            QAST::IVal.new( :value(950) ),
                            QAST::Op.new( :op('bootint') )
                        ),
                        QAST::SVal.new( :value('Abraham') ),
                        QAST::Op.new(
                            :op('box_i'),
                            QAST::IVal.new( :value(175) ),
                            QAST::Op.new( :op('bootint') )
                       )
                    ))),

            # Add up values.
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('istrue'),
                    QAST::Var.new( :name('it'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('add_i'),
                    QAST::Op.new(
                        :op('unbox_i'),
                        QAST::Op.new(
                            :op('iterval'),
                            QAST::Op.new(
                                :op('shift'),
                                QAST::Var.new( :name('it'), :scope('local') )
                    ))),
                    QAST::Op.new(
                        :op('unbox_i'),
                        QAST::Op.new(
                            :op('iterval'),
                            QAST::Op.new(
                                :op('shift'),
                                QAST::Var.new( :name('it'), :scope('local') )
                    )))
               )),

            # Check it's now empty (false).
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('istrue'),
                    QAST::Var.new( :name('it'), :scope('local') )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n1125\n0\n",
    "Hash iteration");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('l'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('splice'),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::IVal.new( :value(2) ),
                QAST::IVal.new( :value(0) )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                ))
        );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "2\n4\n",
    "Splice has the right elems");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('l'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('splice'),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::IVal.new( :value(2) ),
                QAST::IVal.new( :value(2) )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                ))
        );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "2\n4\n",
    "Splice has the right elems at the end");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('l'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('splice'),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::Var.new( :name('l'), :scope('local') ),
                QAST::IVal.new( :value(0) ),
                QAST::IVal.new( :value(2) )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                ))
        );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "2\n2\n",
    "Splice has the right elems replacing two");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('l'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') )
                    )
                )
            ),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new(
                        :scope('positional'),
                        QAST::Var.new( :name('l'), :scope('local') ),
                        QAST::IVal.new( :value(0) )
                    )
                )),
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new(
                    :scope('positional'),
                    QAST::Var.new( :name('l'), :scope('local') ),
                    QAST::IVal.new( :value(1) )
                ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('l'), :scope('local') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new(
                        :scope('positional'),
                        QAST::Var.new( :name('l'), :scope('local') ),
                        QAST::IVal.new( :value(0) )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new(
                        :scope('positional'),
                        QAST::Var.new( :name('l'), :scope('local') ),
                        QAST::IVal.new( :value(1) )
                    )
                )),
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n3\n2\n3\n2\n",
    "QAST::Var positional scope");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('h'), :scope('local'), :decl('var') ),
                QAST::Op.new(
                    :op('hash'),
                    QAST::SVal.new( :value('whisky') ),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') ),
                        QAST::Op.new( :op('list') )
                    ),
                    QAST::SVal.new( :value('vodka') ),
                    QAST::Op.new(
                        :op('list'),
                        QAST::Op.new( :op('list') )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new(
                        :scope('associative'),
                        QAST::Var.new( :name('h'), :scope('local') ),
                        QAST::SVal.new( :value('vodka') )
                    )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new(
                        :scope('associative'),
                        QAST::Var.new( :name('h'), :scope('local') ),
                        QAST::SVal.new( :value('whisky') )
                    )
                )),
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new(
                    :scope('associative'),
                    QAST::Var.new( :name('h'), :scope('local') ),
                    QAST::SVal.new( :value('whisky') )
                ),
                QAST::Op.new(
                    :op('list'),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') ),
                    QAST::Op.new( :op('list') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new(
                        :scope('associative'),
                        QAST::Var.new( :name('h'), :scope('local') ),
                        QAST::SVal.new( :value('whisky') )
                    )
                ))
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1\n2\n3\n",
    "QAST::Var associative scope");
