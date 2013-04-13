#!./parrot nqp.pbc

# combination of conditional modifier and loop modifier

plan(11);

my $a; my $s;


$a := 0; $s := 0;
$s := 5 if $a > 7 while $a++ < 9;
ok( $s == 5 && $a == 10, 'true if + while');

$a := 0; $s := 0;
$s := 5 if $a > 17 while $a++ < 9;
ok( $s == 0 && $a == 10, 'false if + while');

$a := 0; $s := 0;
$s := 5 if $a > 7 until $a++ > 9;
ok( $s == 5 && $a == 11, 'true if + until');

$a := 0; $s := 0;
$s := 5 if $a > 17 until $a++ > 9;
ok( $s == 0 && $a == 11, 'false if + until');

$a := 0; $s := 0;
$s := 5 unless $a > 0 while $a++ < 9;
ok( $s == 0 && $a == 10, 'true unless + while');

$a := 0; $s := 0;
$s := 5 unless $a < 0 while $a++ < 9;
ok( $s == 5 && $a == 10, 'false unless + while');

$a := 0; $s := 0;
$s := 5 if $a > 0 until $a++ > 9;
ok( $s == 5 && $a == 11, 'true if + until');

$a := 0; $s := 0;
$s := 5 if $a < 0 until $a++ > 9;
ok( $s == 0 && $a == 11, 'false if + until');

# Ensure that close curly can end a statement
{ ok(1, "correct parse"); $a := 10; }
while $a == 10 { ok($a == 10, 'while still works'); $a++; }

$a := 1;
$a := $a * $_ for <1 2 3>;
ok( $a == 6 , 'for');

