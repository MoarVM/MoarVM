#!/usr/bin/env perl
use strict;
use Data::Dumper;
my %names;
my %labels;
my @instructions;
my $next_global = 0;
my $riprel = undef;

while (<>) {
    chomp;

    next unless m/^\s*([A-F0-9]+):\s+([A-F0-9]{2} ?)+\s+(\w+)\s+(.+)$/i;
    my ($addr, $opcode, $arg) = (hex($1), $3, $4);

    # remove lines with value bytes
    next if $opcode =~ m/^[0-9A-F]{2}$/i;
    # remove comments
    $arg =~ s/\s+(#.+)$//;

    # rip-relative labels are defined by their end position, hence
    # they are calculate from the address of the next instruction
    if (defined $riprel) {
        $labels{$addr+$riprel} = 1;
        $riprel = undef;
    }
    if ($opcode =~ m/^j\w+$/ && $arg =~ m/^0x/) {
        my $pos = hex($arg);
        $labels{$pos} = 1;
    } elsif ($arg =~ m/\[rip\+(0x[0-9a-f]+)/i)  {
        $riprel = hex($1);
    } elsif ($opcode eq 'movabs') {
        my ($reg,$val) = split /,/, $arg;
        $names{$val} = sprintf('global_%03d', ++$next_global) unless exists $names{$val};
    }
    push @instructions, [$addr, $opcode, $arg];
}

sub sortn { sort { $a <=> $b } @_ }
# assign labels in code order
@labels{sortn keys %labels} = 1..(scalar keys %labels);

for (my $i = 0; $i < @instructions; $i++) {
    my ($addr, $opcode, $arg) = @{$instructions[$i]};
    if (exists $labels{$addr}) {
        # label_ is 6 char, 3 num, 1 colon, 2 space
        print sprintf("label_%03d:  ", $labels{$addr});
    } else {
        print ' ' x 12;
    }
    if ($opcode eq 'movabs') {
        my ($reg,$val) = split /,/, $arg;
        $arg = sprintf('%s,%s', $reg, $names{$val});
    } elsif ($opcode =~ m/^\j\w+$/ && $arg =~ m/^0x/) {
        $arg = sprintf('label_%03d', $labels{hex($arg)});
    } elsif ($arg =~ m/\[rip\+(0x[0-9a-f]+)\]/i) {
        my $pos = hex($1) + $instructions[$i+1]->[0];
        $arg = substr($arg,0,$-[0]) . sprintf('label_%03d # rip', $labels{$pos});
    }
    print "$opcode $arg\n";
}
