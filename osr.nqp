#!/usr/bin/env nqp-m

sub test-osr() {
    my int $i := 0;
    while $i < 200 {
        $i := $i + 1;
        nqp::say("OH HAI $i");
    }
}

test-osr();