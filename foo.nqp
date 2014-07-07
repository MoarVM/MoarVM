#!/usr/bin/env nqp-m

sub bar() {
    1;
}

sub foo() {
    bar() + 3;
}

my int $i := 0;
while $i < 50 {
    $i := $i + 1;
    nqp::say(foo());
 }
