#! nqp

# Test nqp::op pseudo-functions.

plan(113);


ok( nqp::add_i(5,2) == 7, 'nqp::add_i');
ok( nqp::sub_i(5,2) == 3, 'nqp::sub_i');
ok( nqp::mul_i(5,2) == 10, 'nqp::mul_i');
ok( nqp::div_i(5,2) == 2, 'nqp::div_i');

ok( nqp::add_n(5,2) == 7, 'nqp::add_n');
ok( nqp::sub_n(5,2) == 3, 'nqp::sub_n');
ok( nqp::mul_n(5,2) == 10, 'nqp::mul_n');
ok( nqp::div_n(5,2) == 2.5e0, 'nqp::div_n');

ok( nqp::chars('hello') == 5, 'nqp::chars');
ok( nqp::concat('hello ', 'world') eq 'hello world', 'nqp::concat');
ok( nqp::join(' ', ('abc', 'def', 'ghi')) eq 'abc def ghi', 'nqp::join');
ok( nqp::index('rakudo', 'do') == 4, 'nqp::index found');
ok( nqp::index('rakudo', 'dont') == -1, 'nqp::index not found');
ok( nqp::chr(120) eq 'x', 'nqp::chr');
ok( nqp::ord('xyz') eq 120, 'nqp::ord');
ok( nqp::lc('Hello World') eq 'hello world', 'nqp::downcase');
ok( nqp::uc("Don't Panic") eq "DON'T PANIC", 'nqp::upcase');
ok( nqp::flip("foo") eq "oof", "nqp::flip");

my @items := nqp::split(' ', 'a little lamb');
ok( nqp::elems(@items) == 3 && @items[0] eq 'a' && @items[1] eq 'little' && @items[2] eq 'lamb', 'nqp::split');
ok( nqp::elems(nqp::split(' ', '')) == 0, 'nqp::split zero length string');
ok( nqp::elems(nqp::split('\\s', 'Mary had a little lamb')) == 1, 'nqp::split no match');
@items := nqp::split('', 'a man a plan');
ok( nqp::elems(@items) == 12, 'nqp::split zero length delimiter');
@items := nqp::split('a', 'a man a plan a canal panama');
ok( nqp::elems(@items) == 11 && @items[0] eq '' && @items[10] eq '', 'nqp::split delimiter at ends');

ok( nqp::iseq_i(2, 2) == 1, 'nqp::iseq_i');

ok( nqp::cmp_i(2, 0) ==  1, 'nqp::cmp_i');
ok( nqp::cmp_i(2, 2) ==  0, 'nqp::cmp_i');
ok( nqp::cmp_i(2, 5) == -1, 'nqp::cmp_i');

ok( nqp::cmp_n(2.5, 0.5) ==  1, 'nqp::cmp_n');
ok( nqp::cmp_n(2.5, 2.5) ==  0, 'nqp::cmp_n');
ok( nqp::cmp_n(2.5, 5.0) == -1, 'nqp::cmp_n');

ok( nqp::cmp_s("c", "a") ==  1, 'nqp::cmp_s');
ok( nqp::cmp_s("c", "c") ==  0, 'nqp::cmp_s');
ok( nqp::cmp_s("c", "e") == -1, 'nqp::cmp_s');

my @array := ['zero', 'one', 'two'];
ok( nqp::elems(@array) == 3, 'nqp::elems');

ok( nqp::if(0, 'true', 'false') eq 'false', 'nqp::if(false)');
ok( nqp::if(1, 'true', 'false') eq 'true',  'nqp::if(true)');
ok( nqp::unless(0, 'true', 'false') eq 'true', 'nqp::unless(false)');
ok( nqp::unless(1, 'true', 'false') eq 'false',  'nqp::unless(true)');

my $a := 10;

ok( nqp::if(0, ($a++), ($a--)) == 10, 'nqp::if shortcircuit');
ok( $a == 9, 'nqp::if shortcircuit');

ok( nqp::pow_n(2.0, 4) == 16.0, 'nqp::pow_n');
ok( nqp::neg_i(5) == -5, 'nqp::neg_i');
ok( nqp::neg_i(-10) == 10, 'nqp::neg_i');
ok( nqp::neg_n(5.2) == -5.2, 'nqp::neg_n');
ok( nqp::neg_n(-10.3) == 10.3, 'nqp::neg_n');
ok( nqp::abs_i(5) == 5, 'nqp::abs_i');
ok( nqp::abs_i(-10) == 10, 'nqp::abs_i');
ok( nqp::abs_n(5.2) == 5.2, 'nqp::abs_n');
ok( nqp::abs_n(-10.3) == 10.3, 'nqp::abs_n');

ok( nqp::ceil_n(5.2) == 6.0, 'nqp::ceil_n');
ok( nqp::ceil_n(-5.2) == -5.0, 'nqp::ceil_n');
ok( nqp::ceil_n(5.0) == 5.0, 'nqp::ceil_n');
ok( nqp::ceil_n(-5.0) == -5.0, 'nqp::ceil_n');
ok( nqp::floor_n(5.2) == 5.0, 'nqp::floor_n');
ok( nqp::floor_n(-5.2) == -6.0, 'nqp::floor_n');
ok( nqp::floor_n(5.0) == 5.0, 'nqp::floor_n');
ok( nqp::floor_n(-5.0) == -5.0, 'nqp::floor_n');

ok( nqp::substr('rakudo', 1, 3) eq 'aku', 'nqp::substr');
ok( nqp::substr('rakudo', 1) eq 'akudo', 'nqp::substr');
ok( nqp::substr('rakudo', 6, 3) eq '', 'nqp::substr');
ok( nqp::substr('rakudo', 6) eq '', 'nqp::substr');
ok( nqp::substr('rakudo', 0, 4) eq 'raku', 'nqp::substr');
ok( nqp::substr('rakudo', 0) eq 'rakudo', 'nqp::substr');

ok( nqp::x('abc', 5) eq 'abcabcabcabcabc', 'nqp::x');
ok( nqp::x('abc', 0) eq '', 'nqp::x');

ok( nqp::not_i(0) == 1, 'nqp::not_i');
ok( nqp::not_i(1) == 0, 'nqp::not_i');
ok( nqp::not_i(-1) == 0, 'nqp::not_i');

ok(nqp::escape("\b \n \r \f \t \" \\ \e foo") eq '\\b \\n \\r \\f \\t \\" \\\\ \e foo','nqp::escape');
my $var := 'foo';
ok(nqp::escape($var) eq 'foo','nqp::escape works with literal');

ok( nqp::isnull(nqp::null()) == 1, 'nqp::isnull/nqp::null' );
ok( nqp::isnull("hello") == 0, '!nqp::isnull(string)' );
ok( nqp::isnull(13232) == 0, 'nqp::isnull(number)' );

ok( nqp::istrue(0) == 0, 'nqp::istrue');
ok( nqp::istrue(1) == 1, 'nqp::istrue');
ok( nqp::istrue('') == 0, 'nqp::istrue');
ok( nqp::istrue('0') == 0, 'nqp::istrue');
ok( nqp::istrue('no') == 1, 'nqp::istrue');
ok( nqp::istrue(0.0) == 0, 'nqp::istrue');
ok( nqp::istrue(0.1) == 1, 'nqp::istrue');
ok( nqp::istrue(nqp::list()) == 0, 'nqp::istrue on empty list');
ok( nqp::istrue(nqp::list(1,2,3)) == 1, 'nqp::istrue on nonempty list');

my $list := nqp::list(0, 'a', 'b', 3.0);
ok( nqp::elems($list) == 4, 'nqp::elems');
ok( nqp::atpos($list, 0) == 0, 'nqp::atpos');
ok( nqp::atpos($list, 2) eq 'b', 'nqp::atpos');
nqp::push($list, 'four');
ok( nqp::elems($list) == 5, 'nqp::push');
ok( nqp::shift($list) == 0, 'nqp::shift');
ok( nqp::pop($list) eq 'four', 'nqp::pop');
my $iter := nqp::iterator($list);
ok( nqp::shift($iter) eq 'a', 'nqp::iterator');
ok( nqp::shift($iter) eq 'b', 'nqp::iterator');
ok( nqp::shift($iter) == 3.0, 'nqp::iterator');
ok( nqp::elems($list) == 3, "iterator doesn't modify list");
ok( nqp::islist($list), "nqp::islist works");

my $qlist := nqp::qlist(0, 'a', 'b', 3.0);
ok( nqp::elems($qlist) == 4, 'nqp::elems');
ok( nqp::atpos($qlist, 0) == 0, 'nqp::atpos');
ok( nqp::atpos($qlist, 2) eq 'b', 'nqp::atpos');
nqp::push($qlist, 'four');
ok( nqp::elems($qlist) == 5, 'nqp::push');
ok( nqp::shift($qlist) == 0, 'nqp::shift');
ok( nqp::pop($qlist) eq 'four', 'nqp::pop');
my $qiter := nqp::iterator($qlist);
ok( nqp::shift($qiter) eq 'a', 'nqp::iterator');
ok( nqp::shift($qiter) eq 'b', 'nqp::iterator');
ok( nqp::shift($qiter) == 3.0, 'nqp::iterator');
ok( nqp::elems($qlist) == 3, "iterator doesn't modify qlist");
ok( nqp::islist($qlist), "nqp::islist works");

my %hash;
%hash<foo> := 1;
ok( nqp::existskey(%hash,"foo"),"existskey with existing key");
ok( !nqp::existskey(%hash,"bar"),"existskey with missing key");

my @arr;
@arr[1] := 3;
ok(!nqp::existspos(@arr, 0), 'existspos with missing pos');
ok(nqp::existspos(@arr, 1), 'existspos with existing pos');
ok(!nqp::existspos(@arr, 2), 'existspos with missing pos');
ok(nqp::existspos(@arr, -1), 'existspos with existing pos');
ok(!nqp::existspos(@arr, -2), 'existspos with missing pos');
ok(!nqp::existspos(@arr, -100), 'existspos with absurd values');
@arr[1] := NQPMu;
ok(nqp::existspos(@arr, 1), 'existspos with still existing pos');
