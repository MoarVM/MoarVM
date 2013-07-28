use MASTTesting;

plan(9);

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('add_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(27) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "69\n",
    "Integer addition works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('mul_n'),
                    QAST::NVal.new( :value(1.5) ),
                    QAST::NVal.new( :value(3) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "4.5\n",
    "Floating point multiplication works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('pow_n'),
                    QAST::NVal.new( :value(2) ),
                    QAST::NVal.new( :value(10) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "1024\n",
    "pow_n works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('abs_i'),
                    QAST::IVal.new( :value(-123) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "123\n",
    "abs_i works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('sqrt_n'),
                    QAST::NVal.new( :value(256) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "16\n",
    "sqrt_n works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('gcd_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(30) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "6\n",
    "gcd_i works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('lcm_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(30) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "210\n",
    "lcm_i works");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('iseq_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(27) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('iseq_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(42) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('isgt_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(27) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('islt_i'),
                    QAST::IVal.new( :value(42) ),
                    QAST::IVal.new( :value(27) )
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
    "Integer relationals work");

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('iseq_n'),
                    QAST::NVal.new( :value(4.2) ),
                    QAST::NVal.new( :value(2.7) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('iseq_n'),
                    QAST::NVal.new( :value(4.2) ),
                    QAST::NVal.new( :value(4.2) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('isgt_n'),
                    QAST::NVal.new( :value(4.2) ),
                    QAST::NVal.new( :value(2.7) )
                )),
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('islt_n'),
                    QAST::NVal.new( :value(4.2) ),
                    QAST::NVal.new( :value(2.7) )
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
    "Numeric relationals work");
