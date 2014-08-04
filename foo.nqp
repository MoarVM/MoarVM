#!/usr/bin/env nqp-m

sub quam() {
    my num $i := 0.5;
    while $i < 100.0 {
        $i := $i * 1.1;
        nqp::say("Value of \$i is $i");
    }
}

my int $i := 0;
while $i < 100 {
    quam();
    $i := $i + 1;
}