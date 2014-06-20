#!/usr/bin/env nqp-m

# This fibonacci number calculator will be compiled to JIT code

sub fib (int $x) {
    my int $a := 1;
    my int $b := 1;
    while $x > 2 {
        $a := $a + $b;
        $b := $a - $b;
        $x := $x - 1;
    }
    $a
}

sub foo(int $i) {
    nqp::say($i);
}
my int $i := 0;
while $i < 50 {
    $i := $i + 1;
    foo($i);
}