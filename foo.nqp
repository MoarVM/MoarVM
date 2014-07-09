#!/usr/bin/env nqp-m

class Quix {
    method bar() {
        "OH HAI";
    }
}

class Quam {
    method bar() {
        "SUCH WOW";
    }
}

sub foo($o) {
    $o.bar;
}

my $a := Quix.new;
my $b := Quam.new;
my $s;

my int $i := 0;
while $i < 50 {
    $i := $i + 1;
    if $i % 2 == 0 {
        $s := foo($a);
    }
    else {
        $s := foo($b);
    }
    nqp::say($s);
 }
