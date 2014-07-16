#!/usr/bin/env nqp-m

class Quix {
    has num $!foo;

    method new($foo) {
        my $obj := nqp::create(self);
        $obj.BUILD($foo);
        $obj
    }

    method BUILD($foo) {
        $!foo := $foo;
    }

    method Num() {
#        nqp::say("Numify: " ~ $!foo);
        $!foo := $!foo * 1.1;
        $!foo;
    }

    method Bool() {
#        nqp::say("Boolify");
        $!foo < 100;
    }
}

sub bar($o) {
    my num $n := $o * 3.14;
    if $o {
        nqp::say($n);
    } else {
        nqp::say("DONE");
    }
}

my int $i := 0;
my $o := Quix.new(1.0);
while $i < 50 {
    $i := $i + 1;
    bar($o);
}