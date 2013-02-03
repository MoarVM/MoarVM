use MASTTesting;

plan(6);

my @ops := (
  ("bitor_i", 123, 21, "127"),
  ("bitxor_i", 123, 21, "110"),
  ("bitand_i", 123, 21, "17"),
  ("bitshiftl_i", 3, 2, "12"),
  ("bitshiftr_i", 5, 2, "1"),
);

for @ops -> $op {
    qast_test(
        -> {
            my $block := QAST::Block.new(
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op($op[0]),
                        QAST::IVal.new( :value($op[1]) ),
                        QAST::IVal.new( :value($op[2]) )
                    )));
            QAST::CompUnit.new(
                $block,
                :main(QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                )))
        },
        $op[3] ~ "\n",
        $op[0]);
}

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op("bitneg_i"),
                    QAST::IVal.new( :value(3) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "-4\n",
    "bitneg_i");
