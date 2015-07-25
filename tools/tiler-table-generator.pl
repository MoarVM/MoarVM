#!/usr/bin/env perl
use Data::Dumper;
use sexpr;
use warnings;
use strict;

# This script takes the tiler grammar file (x64.tiles)
# and produces tiler tables. Tiler tables conceptually map
# (operations, ruleset*) to ruleset and (possibly) tiles.

 


# Used for getting all combinations belonging to a rule
#
# Algorithm:
# list -> item + list | nil
# nil -> nil
# item + list -> (combinations(list), item + combinations(list))

sub combinations {
    my @list = @_;
    if (@list) {
        my ($top, @rest) = @list;
        my @others = combinations(@rest);
        my @mine;
        for my $c (@others) {
            push @mine, [$top, @$c];
        }
        return (@mine, @others);
    } else {
        return [];
    }
}


# Collect rules -> form list, table;
# list contains 'shallow' nodes, maps rulenr -> rule 
# indirectly create rulenr -> terminal
# table contains head -> rulenr
my @rules;
my %heads;
sub add_rule {
    my ($fragment, $terminal, $depth) = @_;
    my $list = [];
    # replace all sublist with pseudorules
    for my $item (@$fragment) {
        if (ref($item) eq 'ARRAY') {
            my $label = sprintf('L%dP%d', scalar @rules, ++$depth);
            add_rule($item, $label, $depth);
            push @$list, $label;
            # create pseudorule
        } else {
            push @$list, $item;
        }
    }
    push @rules, [$list, $terminal];
    my $rulenr = scalar @rules;
    my $head   = $fragment->[0];
    push @{$heads{$head}}, $rulenr;
}


open my $input, '<', 'src/jit/x64.tiles' or die "Could not read tileset";
my $parser = sexpr->parser($input);
while (my $tree = $parser->read) {
    my $keyword = shift @$tree;
    if ($keyword eq 'tile:') {
        add_rule($tree->[0], $tree->[1], 1);
    }
}
print Dumper(\%heads);


