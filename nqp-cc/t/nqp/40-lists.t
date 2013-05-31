#! nqp

plan(18);

my $a;
$a := (8);
ok( !nqp::islist($a), 'basic parens');

$a := (8,9);
ok( nqp::islist($a), 'paren list');
ok( +$a == 2, 'paren list elems' );

$a := (8,);
ok( nqp::islist($a), 'paren comma');
ok( +$a == 1, 'paren comma' );

$a := ();
ok( nqp::islist($a), 'empty parens');
ok( +$a == 0, 'paren list elems' );

$a := [8];
ok( nqp::islist($a), 'brackets of one elem');
ok( +$a == 1, 'brackets of one elem' );

$a := [7,8,9];
ok( nqp::islist($a), 'brackets of 3 elems');
ok( +$a == 3, 'brackets of 3 elems' );

$a := [];
ok( nqp::islist($a), 'brackets of 0 elems');
ok( +$a == 0, 'brackets of 0 elems' );

$a := {};
ok( nqp::ishash($a), 'empty braces');

$a := { 1 };
ok( !nqp::ishash($a), 'non-empty braces');

sub xyz(*@a) {
    ok( +@a == 1, "brackets as single argument #1" );
    ok( +@a[0] == 2, "brackets as single argument #2");
    ok( @a[0][1] == 'b', "brackets as single argument #3");
}

xyz(['a', 'b']);

