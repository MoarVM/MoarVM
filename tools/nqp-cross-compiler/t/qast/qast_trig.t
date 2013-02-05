use MASTTesting;

plan(13);

my @ops := (
  ("sin_n", 1, "0.8414709848078965"),
  ("asin_n", 1, "1.5707963267948966"),
  ("cos_n", 1, "0.5403023058681398"),
  ("acos_n", 1, "0"),
  ("tan_n", 1, "1.5574077246549023"),
  ("atan_n", 1, "0.7853981633974483"),
  ("sec_n", 1, "1.8508157176809255"),
  ("asec_n", 1, "0"),
  ("sinh_n", 1, "1.1752011936438014"),
  ("cosh_n", 1, "1.543080634815244"),
  ("tanh_n", 1, "0.7615941559557649"),
  ("sech_n", 1, "0.6480542736638853"),
);

for @ops -> $op {
    qast_test(
        -> {
            my $block := QAST::Block.new(
                QAST::Op.new(
                    :op('say'),
                    QAST::Op.new(
                        :op($op[0]),
                        QAST::NVal.new( :value($op[1]) )
                    )));
            QAST::CompUnit.new(
                $block,
                :main(QAST::Op.new(
                    :op('call'),
                    QAST::BVal.new( :value($block) )
                )))
        },
        $op[2] ~ "\n",
        $op[0] ~ " works", :approx(1));
}

qast_test(
    -> {
        my $block := QAST::Block.new(
            QAST::Op.new(
                :op('say'),
                QAST::Op.new(
                    :op('atan2_n'),
                    QAST::NVal.new( :value(0.5) ),
                    QAST::NVal.new( :value(0.5) )
                )));
        QAST::CompUnit.new(
            $block,
            :main(QAST::Op.new(
                :op('call'),
                QAST::BVal.new( :value($block) )
            )))
    },
    "0.7853981633974483\n",
    "atan2_n works", :approx(1));
