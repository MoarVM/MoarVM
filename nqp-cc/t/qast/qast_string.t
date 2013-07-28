use MASTTesting;

plan(11);

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('chars'),
                    QAST::SVal.new( :value("larva") )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "5\n",
    "chars");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('uc'),
                    QAST::SVal.new( :value("laRva") )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "LARVA\n",
    "uc");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('lc'),
                    QAST::SVal.new( :value("laRva") )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "larva\n",
    "lc");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('x'),
                    QAST::SVal.new( :value("boo") ),
                    QAST::IVal.new( :value(2) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "booboo\n",
    "x");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('concat'),
                    QAST::SVal.new( :value("boo") ),
                    QAST::SVal.new( :value("berry") )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "booberry\n",
    "concat");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('chr'),
                    QAST::IVal.new( :value(66) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "B\n",
    "chr");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('iseq_s'),
                    QAST::SVal.new( :value('bacon') ),
                    QAST::SVal.new( :value('cheese') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('iseq_s'),
                    QAST::SVal.new( :value('bacon') ),
                    QAST::SVal.new( :value('bacon') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('isne_s'),
                    QAST::SVal.new( :value('bacon') ),
                    QAST::SVal.new( :value('cheese') )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('isne_s'),
                    QAST::SVal.new( :value('bacon') ),
                    QAST::SVal.new( :value('bacon') )
                )),
            );
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "0\n1\n1\n0\n",
    "String relationals work");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('substr'),
                    QAST::SVal.new( :value("slaughter") ),
                    QAST::IVal.new( :value(1) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('substr'),
                    QAST::SVal.new( :value("angelologist") ),
                    QAST::IVal.new( :value(4) ),
                    QAST::IVal.new( :value(3) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "laughter\nlol\n",
    "substr");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('ord'),
                    QAST::SVal.new( :value("monkey") )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('ord'),
                    QAST::SVal.new( :value("monkey") ),
                    QAST::IVal.new( :value(2) ),
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "109\n110\n",
    "ord");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('index'),
                    QAST::SVal.new( :value("monkeymonkeymonkey") ),
                    QAST::SVal.new( :value("monkey") ),
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('index'),
                    QAST::SVal.new( :value("monkeymonkeymonkey") ),
                    QAST::SVal.new( :value("monkey") ),
                    QAST::IVal.new( :value(1) ),
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "0\n6\n",
    "index");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('rindex'),
                    QAST::SVal.new( :value("monkeymonkeymonkey") ),
                    QAST::SVal.new( :value("monkey") ),
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('rindex'),
                    QAST::SVal.new( :value("monkeymonkeymonkey") ),
                    QAST::SVal.new( :value("monkey") ),
                    QAST::IVal.new( :value(11) ),
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "12\n6\n",
    "rindex");
