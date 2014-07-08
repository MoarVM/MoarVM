#!/usr/bin/env nqp-m

sub bar(int $x, int $y) {
    $x * $y;
}

sub foo(int $x) {
    bar($x, $x + $x);
}

my int $i := 0;
while $i < 50 {
    $i := $i + 1;
    nqp::say(foo($i));
 }
