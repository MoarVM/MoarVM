#!/usr/bin/env nqp-m

sub quam() {
    my num $i := 1.0;
    my num $j := 2.0;
    if $j < $i {
        nqp::say("OH HAI");
    } elsif $j > $i {
    nqp::say("OH NO");
    } else {
    nqp::say("BAI");
}
}

my int $i := 0;
while $i < 100 {
    quam();
    $i := $i + 1;
}