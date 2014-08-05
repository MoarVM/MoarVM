#!/usr/bin/env nqp-m
class Foo {
    method a() {
        nqp::say("Foo.a()");
    }
}

class Bar {
    method a() {
        nqp::say("Bar.a()");
    }
}

sub test-it($obj, $meth) {
    nqp::say($obj."$meth"());
}

my int $i := 0;
while $i < 100 { 
    if $i % 2 == 0 {
        test-it(Foo.new(), "a");
        } else {
        test-it(Bar.new(), "a");
    }
    $i := $i + 1;
}