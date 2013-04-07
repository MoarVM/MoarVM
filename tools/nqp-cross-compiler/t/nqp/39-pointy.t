#! nqp

plan(7);

my $count := 1;

my $x := -> $a, $b { ok($a == $count++, $b); }

$x(1, 'basic pointy block');

my $y := -> $a, $b = 2 { ok($b == $count++, $a); }

$y('pointy block with optional');

$y('pointy block with optional + arg', 3);

for <4 pointy4 5 pointy5 6 pointy6> -> $a, $b { ok($a == $count++, $b); }

my $argless := -> { ok(1, 'argumentless pointy parses ok') }
$argless();
