#! nqp
use nqp-mo; # XXX it really should be 'nqpmo'

plan(45);

my $knowhow := nqp::knowhow();
my $bi_type := $knowhow.new_type(:name('TestBigInt'), :repr('P6bigint'));
$bi_type.HOW.compose($bi_type);
sub str($x) { nqp::tostr_I($x) };
sub iseq($x, $y) { nqp::iseq_I($x, nqp::box_i($y, $bi_type)) }
sub box($x) { nqp::box_i($x, $bi_type) }

my $one := box(1);

my $b := nqp::fromstr_I('-123', $one);
my $c := box(-123);

ok(str($b) eq '-123', 'can round-trip negative number (string)');
ok(str($c) eq '-123', 'can round-trip negative number (string) by boxing');
ok(nqp::unbox_i($b) == -123, 'can round-trip negative number by unboxing');
ok(!nqp::iseq_I($one, $b), 'nqp::iseq_I can return false');
ok(nqp::iseq_I($one, $one), 'nqp::iseq_I can return true');
ok(iseq(nqp::mul_I($b, $b, $b), 15129,), 'multiplication');
ok(iseq(nqp::add_I($b, $b, $b),  -246,), 'addition');
ok(nqp::iseq_I(nqp::sub_I($b, $b, $b), nqp::box_i(0, $bi_type)), 'subtraction');
ok(nqp::iseq_I(nqp::div_I($b, $b, $b), $one), 'division');

ok(iseq(nqp::bitshiftl_I($one, 3, $one), 8), 'bitshift left');
ok(iseq($one, 1), 'original not modified by bitshift left');
ok(iseq(nqp::bitshiftr_I(box(16), 4, $one), 1), 'bitshift right');

ok(iseq(nqp::bitand_I(box(0xdead), box(0xbeef), $one), 0x9ead), 'bit and');
ok(iseq(nqp::bitor_I( box(0xdead), box(0xbeef), $one), 0xfeef), 'bit or');
ok(iseq(nqp::bitxor_I(box(0xdead), box(0xbeef), $one), 0x6042), 'bit xor');

ok(iseq(nqp::bitneg_I(box(-123), $one), 122), 'bit negation');

ok(iseq(nqp::bitand_I(nqp::fromstr_I('-1073741825', $one), $one, $one), 1),
    'Bit ops (RT 109740)');

# Now we'll create a type that boxes a P6bigint.
my $bi_boxer := NQPClassHOW.new_type(:name('TestPerl6Int'), :repr('P6opaque'));
$bi_boxer.HOW.add_attribute($bi_boxer, NQPAttribute.new(
    :name('$!value'), :type($bi_type), :box_target(1)
));
$bi_boxer.HOW.add_parent($bi_boxer, NQPMu);
$bi_boxer.HOW.compose($bi_boxer);

# Try some basic operations with it.
my $box_val_1 := nqp::box_i(4, $bi_boxer);
ok(iseq($box_val_1, 4), 'can box to a complex type with a P6bigint target');
my $box_val_2 := nqp::fromstr_I('38', $bi_boxer);
ok(iseq($box_val_2, 38), 'can get a bigint from a string with boxing type');
ok(iseq(nqp::add_I($box_val_1, $box_val_2, $bi_boxer), 42), 'addition works on boxing type');


# Note that the last argument to pow_I should be capable of boxing a num,
# so $bi_type is wrong here. But so far we only test the integer case,
# so we can get away with it.
my $big := nqp::pow_I($c, box(42), $bi_type, $bi_type);
ok(str($big) eq '5970554685064519004265641008828923248442340700473500698131071806779372733915289638628729', 'pow (int, positive)');
ok(iseq(nqp::pow_I(box(0), $big, $bi_type, $bi_type), 0), 'pow 0 ** large_number');
ok(iseq(nqp::pow_I($one, $big, $bi_type, $bi_type), 1), 'pow 1 ** large_number');

# test conversion to float
# try it with 2 ** 100, because that's big enough not to fit into a single
# int, but can be represented exactly in a double
$big := nqp::pow_I(box(2), box(100), $bi_type, $bi_type);
ok(nqp::iseq_n(nqp::tonum_I($big), nqp::pow_n(2, 100)), '2**100 to float');
$big := nqp::pow_I(box(-2), box(101), $bi_type, $bi_type);
ok(nqp::iseq_n(nqp::tonum_I($big), nqp::pow_n(-2, 101)), '(-2)**101 to float');
# the mantissa can hold much information accurately, so test that too
my $factor := 123456789;
$big := nqp::mul_I($big, box($factor), $bi_type);
ok(nqp::iseq_n(nqp::tonum_I($big), nqp::mul_n($factor, nqp::pow_n(-2, 101))), "$factor * (-2)**101 to float");

$big := 1e16;
my $converted := nqp::tonum_I(nqp::fromstr_I('10000000000000000', $bi_type));
ok(nqp::abs_n($big - $converted) / $big < 1e-4, 'bigint -> float, 1e16');

my $float := 123456789e240;
ok(nqp::iseq_n($float, nqp::tonum_I(nqp::fromnum_I($float, $bi_type))),
    'to_num and from_num round-trip');
my $small_float := 1e3;
ok(nqp::iseq_n($small_float, nqp::tonum_I(nqp::fromnum_I($small_float, $bi_type))),
    'to_num and from_num round-trip on small floats');
my $medium_float := 1e14;
ok(nqp::iseq_n($medium_float, nqp::tonum_I(nqp::fromnum_I($medium_float, $bi_type))),
    'to_num and from_num round-trip on medium sized floats');
$float := -$float;
ok(nqp::iseq_n($float, nqp::tonum_I(nqp::fromnum_I($float, $bi_type))),
    'to_num and from_num round-trip (negative number)');

ok(nqp::base_I(box(-1234), 10) eq '-1234', 'base_I with base 10');
ok(nqp::base_I(box(-1234), 16) eq '-4D2',  'base_I with base 16');

ok(str(nqp::expmod_I(
    nqp::fromstr_I('2988348162058574136915891421498819466320163312926952423791023078876139', $bi_type),
    nqp::fromstr_I('2351399303373464486466122544523690094744975233415544072992656881240319', $bi_type),
    nqp::fromstr_I('10000000000000000000000000000000000000000', $bi_type),
    $bi_type,
)) eq '1527229998585248450016808958343740453059', 'nqp::expmod_I');

ok(nqp::div_In(box(1234500), box(100)) == 12345, 'div_In santiy');
my $n := nqp::div_In(
    nqp::pow_I(box(203), box(200), $bi_type, $bi_type),
    nqp::pow_I(box(200), box(200), $bi_type, $bi_type),
);
ok(nqp::abs_n($n - 19.6430286394751) < 1e-10, 'div_In with big numbers');

my $maxRand := nqp::fromstr_I('10000000000000000000000000000000000000000', $bi_type);
my $rand := nqp::rand_I($maxRand, $bi_type);
ok(nqp::isle_I(box(0), $rand) && nqp::islt_I($rand, $maxRand), 'nqp::rand_I');

ok(nqp::isprime_I(box(-4), 1) == 0, 'is -4 prime');
ok(nqp::isprime_I(box(0), 1) == 0, 'is 0 prime');
ok(nqp::isprime_I(box(1), 1) == 0, 'is 1 prime');
ok(nqp::isprime_I(box(2), 1) == 1, 'is 2 prime');
ok(nqp::isprime_I(box(4), 1) == 0, 'is 4 prime');
ok(nqp::isprime_I(box(17), 1) == 1, 'is 17 prime');

ok(nqp::iseq_I(nqp::gcd_I(box(18), box(12), $bi_type), box(6)), 'nqp::gcd_I');
ok(nqp::iseq_I(nqp::lcm_I(box(18), box(12), $bi_type), box(36)), 'nqp::lcm_I');
