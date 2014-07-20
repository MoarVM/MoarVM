#!/usr/bin/env nqp-m

sub quam(str $s, int $i, str $t) {
    nqp::say(nqp::x($s, $i) ~ " " ~ $t);
}

my $i := 0;
while $i < 100 {
    $i := $i + 1;
    quam("foo", $i, "bar");
}

