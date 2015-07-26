#!/usr/bin/env perl
use List::Util qw(reduce);
use Data::Dumper;
use sexpr;
use warnings;
use strict;

# This script takes the tiler grammar file (x64.tiles)
# and produces tiler tables.

# Collect rules -> form list, table;
# list contains 'shallow' nodes, maps rulenr -> rule
# indirectly create rulenr -> terminal
# table contains head -> rulenr

my @expr; # used only for keeping the epxressions for easy dumping
my @rules;
my %heads;


sub add_rule {
    my ($fragment, $terminal, $cost, $depth) = @_;
    my $list = [];
    # replace all sublist with pseudorules
    for my $item (@$fragment) {
        if (ref($item) eq 'ARRAY') {
            # create pseudorule
            my $label = sprintf('L%dP%d', scalar @rules, ++$depth);
            # divide costs
            $cost /= 2;
            add_rule($item, $label, $cost, $depth);
            push @$list, $label;
        } else {
            push @$list, $item;
        }
    }
    # NB - only top-level fragments are associated with tiles.
    my $rulenr = scalar @rules;
    push @rules, [$list, $terminal, $cost];
    my $head   = $fragment->[0];
    push @{$heads{$head}}, $rulenr;
    $expr[$rulenr] = sexpr::encode($fragment);
}


my $parser = sexpr->parser(\*DATA);
while (my $tree = $parser->read) {
    my $keyword = shift @$tree;
    if ($keyword eq 'tile:') {
        add_rule($tree->[0], $tree->[1], $tree->[2], 1);
    }
}

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


# Generate rulesets to represent all DFA states.
my @rulesets;
my %candidates;
for my $head (keys %heads) {
    my @combined = combinations(@{$heads{$head}});
    pop @combined; # remove empty list, always the last item
    for my $combination (@combined) {
        my $ruleset_nr = scalar @rulesets;
        push @rulesets, $combination;
        my %terminals = map { $rules[$_]->[1] => 1 } @$combination;
        for my $term (keys %terminals) {
            push @{$candidates{$term}}, $ruleset_nr;
        }
    }
}

# Invert the ruleset table
my %inversed;
for (my $ruleset_nr = 0; $ruleset_nr < @rulesets; $ruleset_nr++) {
    my $key = join(',', sort(@{$rulesets[$ruleset_nr]}));
    $inversed{$key} = $ruleset_nr;
}

# Calculate minimum cost rule out of a ruleset and a terminal
sub min_cost {
    my ($ruleset_nr, $term) = @_;
    my @applicable = grep { $rules[$_][1] eq $term } @{$rulesets[$ruleset_nr]};
    my $min = reduce { $rules[$a][2] < $rules[$b][2] ? $a : $b } @applicable;
    return $min;
}

# Generate a table, indexed by head, ruleset_nr, ruleset_nr -> ruleset
# and another table, head, ruleset_nr, ruleset_nr -> rule
my %table;
my %trans;
for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
    my ($frag, $term, $cost) = @{$rules[$rule_nr]};
    my ($head, $c1, $c2)     = @$frag;
    if (defined $c1) {
        my $cand1 = $candidates{$c1};
        if (defined $c2) {
            # binary
            my $cand2   = $candidates{$c2};
            for my $rs1 (@$cand1) {
                my $lc1 = min_cost($rs1, $c1);
                for my $rs2 (@$cand2) {
                    my $lc2 = min_cost($rs2, $c2);
                    $table{$head}{$rs1}{$rs2} = [$rule_nr, $lc1, $lc2] if $term eq 'reg';
                    push @{$trans{$head,$rs1,$rs2}}, $rule_nr;
                }
            }
        } else {
            # unary
            for my $rs1 (@$cand1) {
                my $lc1 = min_cost($rs1, $c1);
                $table{$head}{$rs1} = [$rule_nr, $lc1] if $term eq 'reg';
                push @{$trans{$head,$rs1}}, $rule_nr;
            }
        }
    } else {
        # no children
        $table{$head} = [$rule_nr] if $term eq 'reg';
        push @{$trans{$head}}, $rule_nr;
    }
}

my %states;
while (my ($table_key, $rule_nrs) = each(%trans)) {
    my $ruleset_key = join(',', sort(@$rule_nrs));
    my $ruleset_nr  = $inversed{$ruleset_key};
    my ($head, $rs1, $rs2) = split /$;/, $table_key;
    if (defined $rs1) {
        if (defined $rs2) {
            $states{$head}{$rs1}{$rs2} = $ruleset_nr;
        } else {
            $states{$head}{$rs1} = $ruleset_nr;
        }
    } else {
        $states{$head} = $ruleset_nr;
    }
}


## right, now for a testrun - can we actually tile a tree with this thing
my ($tree, $rest) = sexpr->parse('(add (load (const)) (const))');
sub tile {
    my $tree = shift;
    my ($head, $c1, $c2) = @$tree;
    my ($ruleset_nr, $optimum);
    if (defined $c2) {
        my $l1 = tile($c1);
        my $l2 = tile($c2);
        $ruleset_nr = $states{$head}{$l1}{$l2};
        $optimum    = $table{$head}{$l1}{$l2};
    } elsif (defined $c1) {
        my $l1 = tile($c1);
        $ruleset_nr = $states{$head}{$l1};
        $optimum    = $table{$head}{$l1};
    } else {
        $ruleset_nr = $states{$head};
        $optimum    = $table{$head};
    }
    print "Tiled $head to $expr[$optimum->[0]]\n";
    return $ruleset_nr;
}
tile $tree;


__DATA__
# Minimal grammar to test tiler table generator
(tile: (const) reg 2)
(tile: (load reg) reg 5)
(tile: (add reg reg) reg 2)
(tile: (add reg (const)) reg 3)
(tile: (add reg (load reg)) reg 6)
