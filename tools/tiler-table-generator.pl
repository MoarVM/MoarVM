#!/usr/bin/env perl
use List::Util qw(reduce);
use Getopt::Long;
use sexpr;
use warnings;
use strict;

# This script takes the tiler grammar file (x64.tiles)
# and produces tiler tables.
my $PREFIX = "MVM_JIT_";
my $VARNAME = "MVM_jit_tile_";
my $DEBUG   = 0;
my ($INFILE, $OUTFILE, $TESTING);
GetOptions(
    'debug' => \$DEBUG,
    'testing' => \$TESTING,
    'input=s' => \$INFILE,
    'output=s' => \$OUTFILE,
    'prefix=s' => \$PREFIX,
);
if (!defined $INFILE && @ARGV && -f $ARGV[0]) {
    $INFILE = shift @ARGV;
}



# shorthand for numeric sorts
sub sortn {
    sort { $a <=> $b } @_;
}

# Get unique items in tree
sub uniq {
    my %h;
    $h{$_}++ for @_;
    return keys %h;
}





# Collect rules -> form list, table;
# list contains 'shallow' nodes, maps rulenr -> rule
# indirectly create rulenr -> terminal

my (@rules, @names, @paths, @curpath);
sub add_rule {
    my ($fragment, $terminal, $cost, @trace) = @_;
    my $list = [];
    # replace all sublist with pseudorules
    for (my $i = 0; $i < @$fragment; $i++) {
        my $item = $fragment->[$i];
        if (ref($item) eq 'ARRAY') {
            # create pseudorule
            my $label = sprintf('L%dP%d', scalar @rules, scalar @trace);
            # divide costs
            $cost /= 2;
            add_rule($item, $label, $cost, @trace, $i);
            push @$list, $label;
        } else {
            push @curpath, @trace, $i, -1 if $i > 0;
            push @$list, $item;
        }
    }
    push @curpath, @trace, -1 if @$fragment == 1 && @trace > 0;
    # NB - only top-level fragments are associated with tiles.
    my $rulenr = scalar @rules;
    push @rules, [$list, $terminal, $cost];
    return $rulenr;
}

# Open input (file, stdin, DATA)
my $input;
if (defined $INFILE) {
    open $input, '<', $INFILE or die "Could not open $INFILE";
} elsif (! -t STDIN) {
    $input = \*STDIN;
} elsif ($TESTING) {
    $input = \*DATA;
} else {
    die "No input provided\n";
}

# Collect rules from the grammar
my $parser = sexpr->parser($input);
while (my $tree = $parser->read) {
    my ($keyword, $name, $fragment, $terminal, $cost) = @$tree;
    if ($keyword eq 'tile:') {
        @curpath = ();
        my $rulenr = add_rule($fragment, $terminal, $cost);
        $names[$rulenr] = $name;
        $paths[$rulenr] = [@curpath, -1];
    }
}
close $input;

# initialize nonterminal sets, used to determine the terminals
my (%nonterminal_sets, %trie);
$nonterminal_sets{$_->[1]} = [$_->[1]] for @rules;
my ($added, $deleted, $i);
# override hash-key-join character
local $; = ",";

do {
    $i++;
    # lookup table from nonterminals to nonterminalsetnames
    my %lookup;
    while (my ($k, $v) = each %nonterminal_sets) {
        $lookup{$_}{$k} = 1 for @$v;
    }
    $lookup{$_} = [keys %{$lookup{$_}}] for keys %lookup;

    # reinitialize trie
    %trie = ();
    # build it based on the terminal-to-terminalset map
    for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
        my ($head, $n1, $n2) = @{$rules[$rule_nr][0]};
        if (defined $n2) {
            for my $nt_k1 (@{$lookup{$n1}}) {
                for my $nt_k2 (@{$lookup{$n2}}) {
                    $trie{$head, $nt_k1, $nt_k2}{$rule_nr} = $rules[$rule_nr][1];
                }
            }
        } elsif (defined $n1) {
            for my $nt_k1 (@{$lookup{$n1}}) {
                $trie{$head, $nt_k1, -1}{$rule_nr} = $rules[$rule_nr][1];
            }
        } else {
            $trie{$head,-1, -1}{$rule_nr} = $rules[$rule_nr][1];
        }
    }
    # generate new nonterminal-sets
    my %new_nts;
    for my $generated (values %trie) {
        my @nt_set_gen = sort(uniq(values %$generated));
        my $nt_k       = join(':', @nt_set_gen);
        $new_nts{$nt_k} = [@nt_set_gen];
    }
    # Calculate changes
    $deleted = 0;
    for my $k (keys %nonterminal_sets) {
        $deleted++ unless exists $new_nts{$k};
    }
    $added = scalar(keys %new_nts) - scalar(keys %nonterminal_sets) + $deleted;
    print "Added $added and deleted $deleted\n" if $DEBUG;
    %nonterminal_sets = %new_nts;
} while ($added || $deleted);

print "Required $i iterations\n" if $DEBUG;

# Rulesets can now be read off from the trie
my (@rulesets, %inversed, %candidates);
for my $v (values %trie) {
    my @rule_nrs = sortn(keys %$v);
    my $name  = join $;, @rule_nrs;
    next if exists $inversed{$name};
    my $ruleset_nr = scalar @rulesets;
    push @rulesets, [@rule_nrs];
    $inversed{$name} = $ruleset_nr;
    push @{$candidates{$rules[$_][1]}}, $ruleset_nr for (@rule_nrs);
}

$candidates{$_} = [sortn uniq(@{$candidates{$_}})] for keys %candidates;

# Calculate minimun-cost rule for a ruleset in a terminal
my %min_cost;
for (my $i = 0; $i < @rulesets; $i++) {
    my %nonterms;
    push @{$nonterms{$rules[$_][1]}}, $_ for @{$rulesets[$i]};
    while (my ($nt, $match) = each %nonterms) {
        my $best  = reduce { $rules[$a][2] < $rules[$b][2] ? $a : $b } @{$match};
        $min_cost{$nt,$i} = $best;
    }
}

if ($DEBUG) {
    # print them for me to see
    for my $rs (@rulesets) {
        my $key = join $;, @$rs;
        print "$key: ";
        my @expr = map { sexpr::encode($_) } map { $rules[$_][0] } @$rs;
        print join("; ", @expr);
        print "\n";
    }

}

print "Now we have ", scalar @rulesets, " different rulesets\n" if $DEBUG;

# Select the best (i.e. lowest cost) rules to implement this rule with given rulesets
sub select_rules {
    my ($rule_nr, $rs1, $rs2) = @_;
    my ($head, $nt1, $nt2)    = @{$rules[$rule_nr][0]};
    if (defined $rs2) {
        return [$rule_nr, $min_cost{$nt1,$rs1}, $min_cost{$nt2,$rs2}];
    } elsif (defined $rs1) {
        return [$rule_nr, $min_cost{$nt1,$rs1}, -1];
    } else {
        return [$rule_nr, -1, -1]; # nothing to select
    }
}

# Generate state and tile selection tables
my %trans;
my %select;
for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
    my ($frag, $term, $cost) = @{$rules[$rule_nr]};
    my ($head, $nt1, $nt2)     = @$frag;
    if (defined $nt2) {
        for my $rs1 (@{$candidates{$nt1}}) {
            for my $rs2 (@{$candidates{$nt2}}) {
                $trans{$head,$rs1,$rs2}{$rule_nr} = 1;
                $select{$rule_nr}{$rs1}{$rs2} = select_rules($rule_nr, $rs1, $rs2);
            }
        }
    } elsif (defined $nt1) {
        for my $rs1 (@{$candidates{$nt1}}) {
            $trans{$head,$rs1,-1}{$rule_nr} = 1;
            $select{$rule_nr}{$rs1}{-1} = select_rules($rule_nr, $rs1);
        }
    } else {
        $trans{$head,-1,-1}{$rule_nr} = 1;
        $select{$rule_nr}{-1}{-1} = select_rules($rule_nr);
    }
}

sub rule_cost {
    my ($rule_nr, $rs1, $rs2) = @_;
    my $cost = $rules[$rule_nr][2];
    my $best = $select{$rule_nr}{$rs1}{$rs2};
    if ($best->[1] >= 0) {
        $cost += $rules[$best->[1]][2];
    }
    if ($best->[2] >= 0) {
        $cost += $rules[$best->[2]][2];
    }
    return $cost;
}


# Select the optimum rule, from a ruleset, given child node rulesets,
# considering only rules that yield either registers (values) or void
# (statements)
sub optimum_rule {
    my ($rs0, $rs1, $rs2) = @_;
    my @reg  = grep { $rules[$_][1] eq 'reg'  } @{$rulesets[$rs0]};
    my @void = grep { $rules[$_][1] eq 'void' } @{$rulesets[$rs0]};
    if (@reg) {
        # rules matching reg
        my %costs = map { $_ => rule_cost($_,$rs1,$rs2) } @reg;
        return reduce { $costs{$a} < $costs{$b} ? $a : $b } @reg;
    } elsif (@void) {
        # rules matchin void
        my %costs = map { $_ => rule_cost($_,$rs1,$rs2) } @void;
        return reduce { $costs{$a} < $costs{$b} ? $a : $b } @void;
    } else {
        return -1;
    }
}


# translate rule lists to rulesets and expand rule table, so we can
# sort it later
my %table;
while (my ($table_key, $applicable) = each(%trans)) {
    my @rule_nrs    = sortn keys %$applicable;
    my $ruleset_key = join($;, @rule_nrs);
    my $ruleset_nr  = $inversed{$ruleset_key};
    my ($head, $rs1, $rs2) = split /$;/, $table_key;
    $table{$head}{$rs1}{$rs2} = $ruleset_nr;
}




if ($TESTING) {
    ## right, now for a testrun - can we actually tile a tree with this thing
    my ($tree, $rest) = sexpr->parse('(add (load (const)) (const))');
    sub tile {
        my $tree = shift;
        my ($head, $c1, $c2) = @$tree;
        my ($ruleset_nr, $optimum);
        if (defined $c2) {
            my $l1 = tile($c1);
            my $l2 = tile($c2);
            $ruleset_nr = $table{$head}{$l1}{$l2};
            $optimum    = optimum_rule($ruleset_nr, $l1, $l2);
        } elsif (defined $c1) {
            my $l1 = tile($c1);
            $ruleset_nr = $table{$head}{$l1}{-1};
            $optimum    = optimum_rule($ruleset_nr, $l1, -1);
        } else {
            $ruleset_nr = $table{$head}{-1}{-1};
            $optimum    = optimum_rule($ruleset_nr, -1, -1);
        }
        print "Tiled $head to $optimum ", sexpr::encode($rules[$optimum]), "\n";
        return $ruleset_nr;
    }
    tile $tree;
    ($tree, $rest) = sexpr->parse('(add (const) (load (addr (stack))))');
    tile $tree;
} else {
    # Read the expression tree node types in correct order
    my @expr_ops;
    open my $expr_h, '<', 'src/jit/expr.h' or die "Could not open expression definition file";
    while (<$expr_h>) {
        last if m/^#define MVM_JIT_IR_OPS\(_\) \\/;
    }
    while(!eof($expr_h)) {
        my $line = <$expr_h>;
        chomp $line;
        last unless $line =~ m/\\$/;
        next unless $line =~ m/_\((\w+), \d+, \d+, \w+\)/;
        my $op = substr($line, $-[1], $+[1]-$-[1]);
        push @expr_ops, $op;
    }
    close $expr_h;


    # Write tables
    my $output;
    if (defined $OUTFILE) {
        open $output, '>', $OUTFILE or die "Could not open $OUTFILE";
    } else {
        $output = \*STDOUT;
    }

    print $output <<"HEADER";
/* FILE AUTOGENERATED BY $0. DO NOT EDIT.
 * Define tables for tiler DFA. */
HEADER
    print $output "static const MVMint8 ${VARNAME}paths[] = {\n   ";

    my @path_idx;
    my ($numchar, $idx) = (4, 0);
    for (my $i = 0; $i < @paths; $i++) {
        next unless defined $paths[$i];
        my $trace = $paths[$i];
        for my $step (@$trace) {
            my $str = " $step,";
            $numchar += length($str);
            if ($numchar >= 79) {
                print $output "\n   ";
                $numchar = 4 + length($str)
            }
            print $output $str;
        }
        $path_idx[$i] = $idx;
        $idx += @$trace;
    }
    print $output "\n};\n";

    print $output "static const MVMJitTile ${VARNAME}table[] = {\n";
    for (my $i = 0; $i < @rules; $i++) {
        if (defined $names[$i]) {
            my $terminal = uc $rules[$i][1];
            print $output "    { \&${VARNAME}$names[$i], ${VARNAME}paths + $path_idx[$i], ${PREFIX}${terminal} },\n";
        } else {
            print $output "    { NULL, NULL },\n";
        }
    }
    print $output "};\n\n";

    print $output <<"COMMENT";

/* Each table item consists of 5 integers:
 * 0..3 -> lookup key (nodenr, ruleset_1, ruleset_2)
 * 4    -> next state
 * 5    -> optimum rule if this were a root */

/* TODO - I think this table format can be, if we want it, much
 * smaller - for our current table sizes, keys could fit in 32 bits */
COMMENT

    print $output "static MVMint32 ".$VARNAME."states[][6] = {\n";
    for my $expr_op (@expr_ops) {
        my $head = lc $expr_op;
        for my $rs1 (sortn keys %{$table{$head}}) {
            for my $rs2 (sortn keys %{$table{$head}{$rs1}}) {
                my $state   = $table{$head}{$rs1}{$rs2};
                my $optimum = optimum_rule($state, $rs1, $rs2);
                print $output "    { ${PREFIX}${expr_op}, $rs1, $rs2, $state, $optimum },\n";
            }
        }
    }
    print $output "};\n\n";
    print $output <<"COMMENT";
/* Rule selection table. Used in preorder traversal to 'push down' the
 * best tiles to match the tree. */
COMMENT
    print $output "static MVMint32 ${VARNAME}select[][5] = {\n";
    for (my $rule_nr = 0; $rule_nr < @rules; $rule_nr++) {
        for my $rs1 (sortn keys %{$select{$rule_nr}}) {
            for my $rs2 (sortn keys %{$select{$rule_nr}{$rs1}}) {
                my $pick = $select{$rule_nr}{$rs1}{$rs2};
                print $output "    { $rule_nr, $rs1, $rs2, $pick->[1], $pick->[2] },\n";
            }
        }
    }
    print $output "};\n\n";

    print $output <<"LOOKUP";

/* Lookup routine. Implemented here so that we may change it
 * independently from tiler */

static MVMint32 ${VARNAME}states_lookup(MVMThreadContext *tc, MVMint32 node, MVMint32 c1, MVMint32 c2) {
    MVMint32 top    = (sizeof(${VARNAME}states)/sizeof(${VARNAME}states[0]));
    MVMint32 bottom = 0;
    MVMint32 mid = (top + bottom) / 2;
    while (bottom < mid) {
        if (${VARNAME}states[mid][0] < node) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}states[mid][0] > node) {
            top = mid;
            mid = (top + bottom) / 2;
        } else if (${VARNAME}states[mid][1] < c1) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}states[mid][1] > c1) {
            top = mid;
            mid = (top + bottom) / 2;
        } else if (${VARNAME}states[mid][2] < c2) {
            bottom = mid;
            mid    = (top + bottom) / 2;
        } else if (${VARNAME}states[mid][2] > c2) {
            top = mid;
            mid = (top + bottom) / 2;
        } else {
            break;
        }
    }
    if (${VARNAME}states[mid][0] != node ||
        ${VARNAME}states[mid][1] != c1   ||
        ${VARNAME}states[mid][2] != c2)
        return -1;
    return mid;
}
LOOKUP
    close $output;
}

__DATA__
# Minimal grammar to test tiler table generator
(tile: a (stack) reg 1)
(tile: b (addr reg) mem 1)
(tile: c (addr reg) reg 2)
(tile: d (const) reg 2)
(tile: e (load reg) reg 5)
(tile: f (load mem) reg 5)
(tile: g (add reg reg) reg 2)
(tile: h (add reg (const)) reg 3)
(tile: i (add reg (load reg)) reg 6)
(tile: j (add reg (load mem)) reg 6)
