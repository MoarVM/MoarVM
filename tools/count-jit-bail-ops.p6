#!/usr/bin/env perl6-m
use v6;
my %counts;
my $logfile = @*ARGS ?? shift @*ARGS !! %*ENV<MVM_JIT_LOG>;

for lines($logfile.IO) -> $line {
    if $line ~~ /'BAIL:'/ {
        $line ~~ /'<' (\w+) '>'/;
        %counts{$/[0].Str}++;
    }
}

for %counts.sort(*.value).reverse -> $pair {
    say $pair;
}
