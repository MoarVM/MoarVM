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


    method Bool() {
#        nqp::say("Boolify");
        $!foo := $!foo * 1.1;
        $!foo < 50.0;
    }
}

sub bar($o) {
     if nqp::istrue($o) {
        nqp::say("STILL WAITING");
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