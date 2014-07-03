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
    nqp::say($i / 2);
    $i * 3.5;
}


sub bar() {
    nqp::say("OH HAI");
}

my int $x := 42;

sub access_lex() {
    $x := $x + 1;
    nqp::say($x);
}

class Quix {
    has int $!foo;
    
    method bar() {
        $!foo := $!foo + 1;
        $!foo * 2;
    }
}

my $y := Quix.new;
my int $i := 0;
while $i < 50 {
    $i := $i + 1;
    nqp::say($y.bar);
}