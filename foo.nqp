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

sub foo(num $i) {
    nqp::say($i);
}
my num $i := 0.5;
while $i < 50.0 {
    $i := $i + 1.0;
    foo($i);
}