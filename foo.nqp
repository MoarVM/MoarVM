#!/usr/bin/env nqp-m

sub quam() {
    my int $i := 1;
    my int $j := !$i;
    if $j {
        nqp::say("OH HAI");
    } else {
    nqp::say("OH NO");
    }
}

my int $i := 0;
while $i < 100 {
    quam();
    $i := $i + 1;
}