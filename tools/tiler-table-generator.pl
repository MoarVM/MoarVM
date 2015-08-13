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
                    $table{$head}{$rs1}{$rs2} = [$rule_nr, $lc1, $lc2] if ($term eq 'reg' || $term eq 'void');
                    $trans{$head,$rs1,$rs2}->{$rule_nr} = 1;
                }
            }
        } else {
            # unary
            for my $rs1 (@$cand1) {
                my $lc1 = min_cost($rs1, $c1);
                $table{$head}{$rs1} = [$rule_nr, $lc1] if ($term eq 'reg' || $term eq 'void');
                $trans{$head,$rs1}->{$rule_nr} = 1;
            }
        }
    } else {
        # no children
        $table{$head} = [$rule_nr] if $term eq 'reg';
        $trans{$head}->{$rule_nr} = 1;
    }
}


# translate rule lists to rulesets
my %states;
while (my ($table_key, $applicable) = each(%trans)) {
    my @rule_nrs = sortn keys %$applicable;
    my $ruleset_key = join($;, @rule_nrs);
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
        print "Tiled $head to ", sexpr::encode($optimum),sexpr::encode($rules[$optimum->[0]][0]), "\n";
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


    # dump table
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
/* Each table item consists of 7 integers:
 * 0..3 -> lookup key (nodenr, ruleset_1, ruleset_2)
 * 4    -> next state
 * 5..7 -> optimum rule selection (node, child_1, child_2)
 *
 * To improve alignment, we use 8 integers */
COMMENT


    print $output "static MVMint32 ".$VARNAME."states[][8] = {\n";
    for my $expr_op (@expr_ops) {
        my $name = lc $expr_op;
        my $c1 = $table{$name};  # optimum table
        my $s1 = $states{$name}; # state transition table

        next unless defined $c1 && defined $s1;
        if (ref $s1 eq 'HASH') {
            for my $rs1 (sortn keys %$s1) {
                my $s2 = $s1->{$rs1};
                my $c2 = $c1->{$rs1};
                if (ref $s2 eq 'HASH') {
                    for my $rs2 (sortn keys %$s2) {
                        print $output "    { $PREFIX$expr_op, $rs1, $rs2, $s2->{$rs2}, $c2->{$rs2}[0], $c2->{$rs2}[1], $c2->{$rs2}[2] },\n";
                    }
                } else {
                    print $output "    { $PREFIX$expr_op, $rs1, -1, $s2, $c2->[0], $c2->[1], -1 },\n";
                }
            }
        } else {
            print $output "    { $PREFIX$expr_op, -1, -1, $s1, $c1->[0], -1, -1 },\n";
        }
    }
    print $output "};\n\n";
    print $output <<"LOOKUP";
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
