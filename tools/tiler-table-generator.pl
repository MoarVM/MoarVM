#!/usr/bin/env perl
package tiler;
use strict;
use warnings;

use Getopt::Long;
use File::Spec;
use FindBin;
use lib File::Spec->catdir($FindBin::Bin, 'lib');

use sexpr;
use expr_ops;


# Get unique items in tree
sub uniq {
    my %h; grep !$h{$_}++, @_;
}

# shorthand for numeric sorts
sub sortn {
    sort { $a <=> $b } @_;
}

sub register_spec {
    my ($symbol) = @_;
    if ($symbol =~ m/^reg/) {
        return "require($1)" if ($symbol =~ m/:(\w+)$/);
        return 'any';
    } else {
        return 'none';
    }
}

sub symbol_name {
    # remove annotation from symbol
    my $copy = $_[0];
    $copy =~ s/:\w+$//;
    return $copy;
}

sub add_rule {
    my ($name, $tree, $sym, $cost) = @_;
    my $ctx = {
        # lookup path for values
        path => [],
        # specifications of registers
        spec => [],
        # bitmap of referenced symbols (vs raw values)
        refs => 0,
        # number of arguments and refs
        num  => 0,
    };
    push @{$ctx->{'spec'}}, register_spec($sym);

    my @rules = decompose($ctx, $tree, $sym, $cost);
    my $head = $rules[$#rules];
    $head->{name} = $name;
    $head->{path} = join('', @{$ctx->{path}});
    $head->{spec} = $ctx->{spec};
    $head->{refs} = $ctx->{refs};
    $head->{text} = sexpr_encode($tree);
    return @rules;
}


sub new_rule {
    # Build a new, fully decomposed rule
    my ($pat, $sym, $cost) = @_;
    return {
        pat  => $pat,
        sym  => $sym,
        cost => $cost
    };
}


# To generate unique symbols
my $pseudosym = 0;

sub decompose {
    my ($ctx, $tree, $sym, $cost, @trace) = @_;
    my $list  = [];
    my @rules;
    # Recursively replace child nodes by pseudosymbols
    for (my $i = 0; $i < @$tree; $i++) {
        my $item = $tree->[$i];
        if (ref $item eq 'ARRAY') {
            # subtree, which has to be replaced with a symbol
            my $newsym = sprintf("#%s", $pseudosym++);
            # divide cost by two
            $cost /= 2;
            # add rule and subrules to the list
            push @rules, decompose($ctx, $item, $newsym, $cost, @trace, $i);
            push @$list, $newsym;
        } elsif (substr($item, 0, 1) eq '$') {
            # argument symbol
            # add trace to path
            push @{$ctx->{path}}, @trace, $i, '.';
            $ctx->{num}++;
        } else {
            if ($i > 0) {
                # value symbol
                push @{$ctx->{path}}, @trace, $i, '.';
                # this is a value symbol, so add it to the bitmap
                $ctx->{refs} += (1 << $ctx->{num});
                $ctx->{num}++;
                push @{$ctx->{spec}}, register_spec($item);
            } # else head
            push @$list, symbol_name($item);
        }
    }
    push @rules, new_rule($list, symbol_name($sym), $cost);
    return @rules;
}

sub combine_rules {
    my @rules = @_;
    # Use a readable hash key separator
    local $; = ",";

    # %sets represents the symbols which can occur in combination (symsets)
    # %trie is the table that holds all combinations of rules and symsets
    my (%sets, %trie);
    # Initialize the symsets with just their own symbols
    $sets{$_->{sym}} = [$_->{sym}] for @rules;
    my ($added, $deleted, $iterations);
    do {
        $iterations++;
        # Generate a lookup table to translate symbols to the
        # combinations (symsets) they appear in
        my %lookup;
        while (my ($k, $v) = each %sets) {
            # Use a nested hash for set semantics
            $lookup{$_}{$k} = 1 for @$v;
        }
        # Reset trie
        %trie = ();
        # Translate symbols in rule patterns to symsets and use these to
        # build the combinations of matching rules
        for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
            my $rule = $rules[$rule_nr];
            # The head is significant because this represent the expression node we match
            my ($head, $sym1, $sym2) = @{$rule->{pat}};
            if (defined $sym2) {
                # iterate over all symbols in the symsets
                for my $s_k1 (keys %{$lookup{$sym1}}) {
                    for my $s_k2 (keys %{$lookup{$sym2}}) {
                        # This rule could match all combinations of $s_k1 and $s_k2 that appear
                        # here because their matching symbols are contained in these symsets.
                        # Here we are interested in all the other rules that also match these
                        # symsets and the symbols these rules generate in combination. Thus,
                        # we generate a new table here.
                        $trie{$head, $s_k1, $s_k2}{$rule_nr} = $rule->{sym};
                    }
                }
            } elsif (defined $sym1) {
                # Handle the one-item case
                for my $s_k1 (keys %{$lookup{$sym1}}) {
                    $trie{$head, $s_k1, -1}{$rule_nr} = $rule->{sym};
                }
            } else {
                $trie{$head, -1, -1}{$rule_nr} = $rule->{sym};
            }
        }
        # Read the symsets from the generated table, generate a
        # key to identify them and replace the old %sets table
        my %new_sets;
        for my $gen (values %trie) {
            my @set = sort(uniq(values %$gen));
            my $key = join(':', @set);
            $new_sets{$key} = [@set];
        }
        # This loop converges the symsets to an unchanging and complete
        # set of symsets. That seems to be because a symsets is always
        # formed by the combination of other symsets that happen to be
        # applicable to the same rules. The combined symset is still
        # applicable to those rules (thus a symset is never lost, just
        # embedded into a larger symset). When symsets stop changing that
        # must be because they cannot be combined further, and thus the
        # set is complete.
        $deleted = 0;
        for my $k (keys %sets) {
            $deleted++ unless exists $new_sets{$k};
        }
        $added = scalar(keys %new_sets) - scalar(keys %sets) + $deleted;
        # Continue with newly generated sets
        %sets = %new_sets;
    } while ($added || $deleted);

    # Given that all possible symsets are known, we can now read
    # the rulesets from the %trie as well.
    my (%seen, @rulesets);
    for my $symset (values %trie) {
        my @rule_nrs = sortn(keys %$symset);
        my $key = join $;, @rule_nrs;
        push @rulesets, [@rule_nrs] unless $seen{$key}++;
    }
    return @rulesets;
}

sub set_key {
    my @rule_nrs = @_;
    return join ":", sortn(@rule_nrs);
}


# This script takes the tiler grammar file (x64.tiles)
# and produces tiler tables.
my $PREFIX = "MVM_JIT_";
my $VARNAME = "MVM_jit_tile_";
my $EXPR_HEADER_FILE = 'src/jit/expr.h';
my $DEBUG   = 0;
my ($INFILE, $OUTFILE, $TESTING);

sub generate_table {
    # Compute possible combination tables and minimum cost tables from
    # rulesets. Requires rules (pattern + symbol + cost) and rulesets
    # (indices into rules).

    my ($rules, $rulesets) = @_;

    my (%candidates, %trans);
    # map symbols to rulesets, rule set names to ruleset numbers
    for (my $ruleset_nr = 0; $ruleset_nr < @$rulesets; $ruleset_nr++) {
        my $ruleset = $rulesets->[$ruleset_nr];
        for my $rule_nr (@$ruleset) {
            $candidates{$rules->[$rule_nr]{sym}}{$ruleset_nr} = 1;
        }
        my $key = set_key(@$ruleset);
        $trans{$key} = $ruleset_nr;
    }

    # build flat table first
    my %flat;
    for (my $rule_nr = 0; $rule_nr < @$rules; $rule_nr++) {
        my $rule = $rules->[$rule_nr];
        my ($head, $sym1, $sym2) = @{$rule->{pat}};
        if (defined $sym2) {
            for my $rs1 (keys %{$candidates{$sym1}}) {
                for my $rs2 (keys %{$candidates{$sym2}}) {
                    $flat{$head,$rs1,$rs2}{$rule_nr} = 1;
                }
            }
        } elsif (defined $sym1) {
            for my $rs1 (keys %{$candidates{$sym1}}) {
                $flat{$head,$rs1,-1}{$rule_nr} = 1;
            }
        } else {
            $flat{$head,-1,-1}{$rule_nr} = 1;
        }
    }

    # with the flat table, we can directly build the tiler table by expanding the keys
    my %table;
    while (my ($idx, $match) = each %flat) {
        my ($head, $rs1, $rs2) = split $;, $idx;
        my $key = set_key(keys %$match);
        die "Cannot find key $key" unless defined $trans{$key};
        $table{$head}{$rs1}{$rs2} = $trans{$key};
    }
    return %table;
}

sub compute_costs {
    my ($rules, $rulesets, $table) = @_;

    my %reversed;
    for my $head (keys %$table) {
        for my $rs1 (keys %{$table->{$head}}) {
            for my $rs2 (keys %{$table->{$head}->{$rs1}}) {
                my $rsy = $table->{$head}{$rs1}{$rs2};
                push @{$reversed{$rsy}}, [$rs1, $rs2];
            }
        }
    }

    # converge at %min_cost.
    #
    # seed %rule_cost with the minimum-zero-order cost, %min_cost according to that
    # compute first order cost using the seeded zero-order cost,
    # compute minimum cost table again; while it remains changing,
    # compute again with updated costs
    my %rule_cost;
    my %min_cost;

    for (my $ruleset_nr = 0; $ruleset_nr < @$rulesets; $ruleset_nr++) {
        for my $rule_nr (@{$rulesets->[$ruleset_nr]}) {
            my $cost = $rules->[$rule_nr]{cost};
            my $sym  = $rules->[$rule_nr]{sym};
            my $best = $min_cost{$ruleset_nr, $sym};
            if (!defined($best) || $rule_cost{$ruleset_nr, $best} > $cost) {
                $min_cost{$ruleset_nr, $sym} = $rule_nr;
            }
            $rule_cost{$ruleset_nr, $rule_nr} = $cost;
        }
    }

    my $changed = 0;
    do {
        my %new_cost;
        my %new_min;
        for (my $ruleset_nr = 0; $ruleset_nr < @$rulesets; $ruleset_nr++) {
            for my $rule_nr (@{$rulesets->[$ruleset_nr]}) {
                my $cost = $rules->[$rule_nr]->{cost};
                my ($head, $sym1, $sym2) = @{$rules->[$rule_nr]->{pat}};
                # compute new cost of rule
                for my $rsg (@{$reversed{$ruleset_nr}}) {
                    $cost += $rule_cost{$rsg->[0], $min_cost{$rsg->[0], $sym1}} if defined $sym1;
                    $cost += $rule_cost{$rsg->[1], $min_cost{$rsg->[1], $sym2}} if defined $sym2;
                }
                $cost /= scalar @{$reversed{$ruleset_nr}};
                # determine new minimum cost rule
                my $sym = $rules->[$rule_nr]->{sym};
                my $best = $new_min{$ruleset_nr, $sym};
                if (!defined ($best) || $new_cost{$ruleset_nr, $best} > $cost) {
                    $new_min{$ruleset_nr, $sym} = $rule_nr;
                }
                $new_cost{$ruleset_nr, $rule_nr} = $cost;
            }

        }
        $changed = 0;
        # nb, i assume we've converged after the *relative* cost of rules doesn't change,
        # but i only compute whether the *top* rule hasn't changed, and that's actually
        # not sufficient in some cases
        for my $key (keys %min_cost) {
            die "huh $key" if !defined $new_min{$key};
            $changed++ if $min_cost{$key} != $new_min{$key};
        }

        %rule_cost = %new_cost;
        %min_cost  = %new_min;
    } while($changed);
    return %min_cost;
}

# Collect rules -> form list, table;
# list contains 'shallow' nodes, maps rulenr -> rule
# indirectly create rulenr -> terminal

GetOptions(
    'debug' => \$DEBUG,
    'testing' => \$TESTING,
    'input=s' => \$INFILE,
    'output=s' => \$OUTFILE,
    'prefix=s' => \$PREFIX,
    'header=s' => \$EXPR_HEADER_FILE,
);


my @rules;
my $input;
if ($TESTING) {
    $input = \*DATA;
} else {
    if (!defined $INFILE && @ARGV && -f $ARGV[0]) {
        $INFILE = shift @ARGV;
    }
    die "Please provide an input file" unless defined $INFILE;
    open $input, '<', $INFILE or die "Could not open $INFILE";
}


# Collect rules from the grammar
my $parser = sexpr->parser($input);

while (my $tree = $parser->parse) {
    my $keyword = shift @$tree;
    if ($keyword eq 'tile:') {
        # (tile: name pattern symbol cost)
        push @rules, add_rule(@$tree);
    } elsif ($keyword eq 'define:') {
        # (define: pattern symbol)
        push @rules, add_rule(undef, @$tree, 0);
    }
}
close $input;


my @rulesets = combine_rules(@rules);
if ($DEBUG) {
    print "Rules:\n";
    for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
        print "$rule_nr => ";
        print sexpr_encode($rules[$rule_nr]{pat}), ": ", $rules[$rule_nr]{sym} , "\n";
    }

    print "Rulesets:\n";
    for (my $ruleset_nr = 0; $ruleset_nr < @rulesets; $ruleset_nr++) {
        print "$ruleset_nr => ";
        for my $rule_nr (@{$rulesets[$ruleset_nr]}) {
            print "$rule_nr, ";
        }
        print "\n";
    }

}

my %table    = generate_table(\@rules, \@rulesets);
my %min_cost = compute_costs(\@rules, \@rulesets, \%table);

# Tiling works by selecting *possible* rules bottom-up and picking
# the *optimum* rules top-down. So we need to know, starting from
# a rule and it's children's rulesets, how to select the best rules.
my @symbols = uniq(map { $_->{sym} } @rules);
my %symnum;
for (my $i = 0; $i < @symbols; $i++) {
    $symnum{$symbols[$i]} = $i;
}


sub bits {
    my $i = 0;
    my $n = shift;
    while ($n) {
        $i++ if $n & 1;
        $n >>= 1;
    }
    return $i;
}




# Write tables
if (defined $OUTFILE) {
    open my $output, '>', $OUTFILE or die "Could not open $OUTFILE";
    select $output;
}
local $\ = "\n";
print "/* FILE AUTOGENERATED BY $0. DO NOT EDIT. */";
print '/* Tile function declarations */';
for my $tile (grep $_, map $_->{name}, @rules) {
    print "${PREFIX}TILE_DECL($tile);";
}

print '/* Tile template declarations */';
print "static const MVMJitTileTemplate ${VARNAME}templates[] = {";
for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
    my $rule = $rules[$rule_nr];
    my ($head, $sym1, $sym2) = @{$rule->{pat}};
    my $sn1  = defined $sym1 ? $symnum{$sym1} : -1;
    my $sn2  = defined $sym2 ? $symnum{$sym2} : -1;
    my ($func, $path, $text, $refs, $nval, $spec);
    if (exists $rule->{name}) {
        $func = defined $rule->{name} ? "${PREFIX}TILE_NAME($rule->{name})" : "NULL";
        $path = sprintf('"%s"', $rule->{path});
        $text = sprintf('"%s"', $rule->{text});
        $refs = $rule->{refs};
        $nval = bits($refs);
        $spec = join('|', map sprintf('MVM_JIT_REGISTER_ENCODE(MVM_JIT_REGISTER_%s,%d)',
                                      uc $rule->{spec}[$_], $_), 0..$#{$rule->{spec}});
    } else {
        $func = $path = $text = "NULL";
        $refs = 0;
        $nval = 0;
        $spec = 0;
    }
    print qq(    {
        $func,
        $path,
        $text,
        $sn1,
        $sn2,
        $nval,
        $refs,
        $spec
    },);

}
print "};";

print '/* Tiler tables */';
print "static const MVMint32 ${VARNAME}select[][3] = {";
for (my $ruleset_nr = 0; $ruleset_nr < @rulesets; $ruleset_nr++) {
    for (my $sym_nr = 0; $sym_nr < @symbols; $sym_nr++) {
        my $rule = $min_cost{$ruleset_nr,$symbols[$sym_nr]};
        next unless defined $rule;
        print "    { $ruleset_nr, $sym_nr, $rule },";
    }
}
print "};";

print <<"COMMENT";

/* Each table item consists of 5 integers:
 * 0..3 -> lookup key (nodenr, ruleset_1, ruleset_2)
 * 4    -> next state
 * 5    -> optimum rule if this were a root */

/* TODO - I think this table format can be, if we want it, much
 * smaller - for our current table sizes, keys could fit in 32 bits.
 * And we could add the terminals and minimum-cost table as
 * intermediates. */

COMMENT
print "static MVMint32 ".$VARNAME."state[][6] = {";
for my $expr_op (@EXPR_OPS) {
    my $head = lc($expr_op->[0]);
    for my $rs1 (sortn keys %{$table{$head}}) {
        for my $rs2 (sortn keys %{$table{$head}{$rs1}}) {
            my $state = $table{$head}{$rs1}{$rs2};
            my $best  = $min_cost{$state,'reg'} // $min_cost{$state,'void'} // -1;
            printf '    { %s%s, %s, %s, %d, %d },',
                   $PREFIX, $expr_op->[0], $rs1, $rs2, $state, $best;
        }
    }
}
print "};";

print <<"LOOKUP";
/* Lookup routines. Implemented here so that we may change it
 * independently from tiler */

static MVMint32* ${VARNAME}state_lookup(MVMThreadContext *tc, MVMint32 node, MVMint32 c1, MVMint32 c2) {
    MVMint32 top    = (sizeof(${VARNAME}state)/sizeof(${VARNAME}state[0]));
    MVMint32 bottom = 0;
    MVMint32 mid = (top + bottom) / 2;
    while (bottom < mid) {
        if (${VARNAME}state[mid][0] < node) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}state[mid][0] > node) {
            top = mid;
            mid = (top + bottom) / 2;
        } else if (${VARNAME}state[mid][1] < c1) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}state[mid][1] > c1) {
            top = mid;
            mid = (top + bottom) / 2;
        } else if (${VARNAME}state[mid][2] < c2) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}state[mid][2] > c2) {
            top = mid;
            mid = (top + bottom) / 2;
        } else {
            break;
        }
    }
    if (${VARNAME}state[mid][0] != node ||
        ${VARNAME}state[mid][1] != c1   ||
        ${VARNAME}state[mid][2] != c2)
        return NULL;
    return ${VARNAME}state[mid];
}

/* Same as above, maps tile state + nonterm -> child rule, used for
 * downward propagation of optimal rules */

static MVMint32 ${VARNAME}select_lookup(MVMThreadContext *tc, MVMint32 ts, MVMint32 nt) {
    MVMint32 top    = (sizeof(${VARNAME}select)/sizeof(${VARNAME}select[0]));
    MVMint32 bottom = 0;
    MVMint32 mid = (top + bottom) / 2;
    while (bottom < mid) {
        if (${VARNAME}select[mid][0] < ts) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}select[mid][0] > ts) {
            top = mid;
            mid = (top + bottom) / 2;
        } else if (${VARNAME}select[mid][1] < nt) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}select[mid][1] > nt) {
            top = mid;
            mid = (top + bottom) / 2;
        } else {
            break;
        }
    }
    if (${VARNAME}select[mid][0] != ts ||
        ${VARNAME}select[mid][1] != nt)
        return -1;
    return ${VARNAME}select[mid][2];
}
LOOKUP

close STDOUT;



__DATA__
# Minimal grammar to test tiler table generator
(tile: a (stack) reg 1)
(tile: c (addr reg $ofs) reg 2)
(tile: d (const $val) reg 2)
(tile: e (load reg $size) reg 5)
(tile: g (add reg reg) reg 2)
(tile: h (add reg (const $val)) reg 3)
(tile: i (add reg (load reg $size)) reg 6)
