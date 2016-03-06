#!/usr/bin/env perl
use strict;
my %names;
my $label = 1;
while (<>) {
    chomp;
    next unless m/^\s*([A-F0-9]+):\s+([A-F0-9]{2} ?)+\s+(\w+)\s+(.+)$/i;
    my ($addr, $opcode, $arg) = (hex($1), $3, $4);
    next if $opcode =~ m/^[0-9A-F]+$/i;
    $arg =~ s/\s+(#.+)$//;

    if ($opcode =~ m/^j\w+$/ && $arg =~ m/^0x/) {
        my $ofs = hex($arg) - $addr;
        $arg = sprintf("%x", $ofs);
    } elsif ($opcode eq 'movabs') {
        my ($reg,$val) = split /,/, $arg;
        $names{$val} = 'global_'.$label++ unless exists $names{$val};
        $arg = $names{$val};
    }
    print "$opcode $arg\n";
}
