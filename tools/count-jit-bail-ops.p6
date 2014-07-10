#!/usr/bin/env perl6-m
use v6;
my %counts;
for lines(%*ENV{"MVM_JIT_LOG"}.IO) -> $line {
    if $line ~~ m/^'BAIL: op <'(\w+)'>'/ {
        %counts{$/[0].Str}++;
    }
}

for %counts.sort(*.value).reverse -> $pair {
    say $pair;
}
