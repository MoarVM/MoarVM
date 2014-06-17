#!/usr/bin/env nqp-m

sub foo(int $x) {
    if ($x < 50) {
        5;
    } else {
        4;
    }
}

my int $i := 0;

while $i < 100 {
    $i++;
    my int $x := foo($i);
    nqp::say("Return value: " ~ $x);
}
