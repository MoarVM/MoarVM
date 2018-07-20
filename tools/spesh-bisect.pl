#!/usr/bin/env perl

use 5.10.0;

chomp $prog;
my $min = 1;
my $max = 100000;
my $l = 50000;
while ($min < $l and $l < $max) {
    $ENV{MVM_SPESH_LIMIT} = $l;
    say "Trying $l";
    if (system(@ARGV) != 0) {
        $max = $l - 1;
    }
    else {
        $min = $l + 1
    };
    $l = $min + int(($max - $min) / 2);
};
say "MVM_SPESH_LIMIT=$l";
