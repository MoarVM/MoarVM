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

my int $i := 0;
while $i < 50 {
    $i := $i + 1;
    my int $f := fib($i);
    nqp::say("Fib number " ~ $i ~ " is " ~ $f);
}