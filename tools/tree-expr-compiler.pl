#!/usr/bin/env perl
use Test::More;
use Getopt::Long;
use strict;

# A S-EXP is the most trivial thing to parse in the world.  Writing S-EXP
# is greatly preferable to hand-matching tree fragment offsets, We
# need (a lot of) tree fragments for building the low-level expression
# tree IR (the ETIR). Because of this, it seems to me to be simpler to
# build a preprocessor for generating the ETIR template tables, which
# are then used (at runtime) to generate the ET for a basic block.
# This is pretty much exactly the same game as dynasm, except for an
# intermediate format rather than machine code.

# Input:
#   (load (addr pargs $1))
# Output
#   template:(MVM_JIT_ADDR, MVM_JIT_PARGS, 1, MVM_JIT_LOAD, 0), 
#   length: 5, root: 3 "..f..l"
#

sub parse_sexp {
    my $expr = shift;
    my $tree = [];
    # consume initial opening parenthesis
    return (undef, $expr) unless $expr =~ m/^\s*\(/;
    $expr = substr($expr, $+[0]);
    while ($expr) {
        $expr =~ s/^\s*//;
        if (substr($expr, 0, 1) eq '(') {
            my ($child, $rest) = parse_sexp($expr);
            $expr = $rest;
            push @$tree, $child;
        } elsif (substr($expr, 0, 1) eq ')') {
            $expr = substr $expr, 1;
            last;
        } elsif ($expr =~ m/^[#\$]?[\w\.\[\]_]+/) {
            push @$tree, substr($expr, $-[0], $+[0] - $-[0]);
            $expr = substr $expr, $+[0];
        } else {
            die "Could not parse $expr";
        }
    }
    return ($tree, $expr);
}


# Wrapper for the recursive write_template
sub compile_template {
    my $tree = shift;
    my ($templ, $desc, $env) = ([], [], {});
    my ($root, $mode) = write_template($tree, $templ, $desc, $env);
    die "Invalid template!" unless $mode eq 'l'; # top should be a simple expression
    return {
        root => $root, 
        template => $templ, 
        desc => join('', @$desc)
    };
}

my $PREFIX = 'MVM_JIT_';
sub write_template {
    my ($tree, $templ, $desc, $env) = @_;
    die "Can't deal with an empty tree" unless @$tree; # we need at least some nodes
    my $top = $tree->[0]; # get the first item, used for dispatch
    die "First parameter must be a bareword" unless $top =~ m/^[a-z]\w*$/i;
    my (@items, @desc); # accumulate state
    if ($top eq 'let') {
        # deal with let declarations
        die "Using let more than once is illegal and wouldn't DWYM anyway" if %$env;
        my $decl = $tree->[1];
        my $expr = $tree->[2];
        for my $stmt (@$decl) {
            die "Let statement should hold 2 expressions" unless @$stmt == 2;
            die "Variable name {$stmt->[0]} is invalid" unless $stmt->[0] =~ m/\$[a-z]\w*/i;
            die "Let statement expects an expression" unless ref($stmt->[1]) eq 'ARRAY';
            my ($child, $mode) = write_template($stmt->[1], $templ, $desc, $env);
            die "Let can only be used with simple expresions" unless $mode eq 'l';
            $env->{$stmt->[0]} = $child;
        }
        return write_template($expr, $templ, $desc, $env);
    } elsif ($top eq 'sizeof') {
        # Add sizeof expression for the compiler
        die "Invalid sizeof expr" unless @$tree == 2;
        return (sprintf('sizeof(%s)', $tree->[1]), '.');
    } elsif ($top eq 'offsetof') {
        # add offsetof
        die "Invalid offsetof expr" unless @$tree == 3;
        return (sprintf('offsetof(%s, %s)', $tree->[1], $tree->[2]), '.');
    }
    # deal with a simple expression
    for my $item (@$tree) {
        if (ref($item) eq 'ARRAY') {
            # subexpression: get offset and template mode for this root
            my ($child, $mode) = write_template($item, $templ, $desc, $env); 
            push @items, $child; 
            push @desc, $mode;
        } elsif ($item =~ m/^\$\d+$/) {
            # numeric variable (an operand parameter)
            push @items, substr($item, 1)+0; # pass the operand nummer
            push @desc, 'f'; # at run time, fill this from operands
        } elsif ($item =~ m/^\$\w+$/) {
            # named variable (declared in nlet)
            die "Undefined variable '$item' used" unless defined $env->{$item};
            push @items, $env->{$item};
            push @desc, 'l'; # also needs to be linked in properly
        } elsif ($item =~ m/^\d+$/) {
            # integer numerics are passed literally
            push @items, $item;
            push @desc, '.';
        } else {
            # barewords are passed as uppercased prefixed strings
            push @items, $main::PREFIX . uc($item);
            push @desc, '.';
        }
    }
    my $root = @$templ; # current position is where we'll be writing the root template.
    # add to output array
    push @$templ, @items; 
    push @$desc, @desc;
    # a simple expression should be linked in at runtime
    return ($root, 'l');
}

# Not sure why shift can't do this by itself!
sub first(@) {
    return shift;
}

if ($ARGV[0] eq 'test') {
    $main::PREFIX = 'MJ_';
    plan(tests => 25);
    is_deeply(first(parse_sexp('()')), []);
    is_deeply(first(parse_sexp('(foo)')), ['foo']);;
    is_deeply(first(parse_sexp('(foo bar)')), [qw<foo bar>]);
    is_deeply(first(parse_sexp('(foo (bar))')), ['foo', ['bar']]);;
    is_deeply(first(parse_sexp('((foo) (bar))')), [['foo'], ['bar']]);
    is_deeply(first(parse_sexp('(0)')), ['0']);
    eval { compile_template(parse_sexp('()')) }; ok $@, 'Cannot compile empty template';
    eval { compile_template(parse_sexp('(foo bar)')) }; ok !$@, 'a simple expression should work';
    eval { compile_template(parse_sexp('(offsetof foo bar)')) };
    ok $@, 'Template root must be simple expr';
    eval { compile_template(parse_sexp('(foo (sizeof))')) }; ok $@, 'sizeof requires one child';
    eval { compile_template(parse_sexp('(let (($foo (bar)) ($quix (quam $1))) (bar $foo $quix))')) };
    ok !$@, 'let expressions should live and take more than one argument';
    eval { compile_template(parse_sexp('(foo $bar)')) };
    ok $@, 'Cannot compile with undefined variables';
    eval { compile_template(parse_sexp('($1 bar)')) }; ok $@, 'First argument should be bareword';
    my $simple = compile_template(parse_sexp('(foo bar)'));
    is($simple->{root}, 0, 'root of a very simple expression should be 0');
    is($simple->{desc}, '..', 'simple expression without filling or linking');
    my $let = compile_template(parse_sexp('(let (($foo (baz))) (quix $foo))'));
    is($let->{root}, 1, 'baz expression requires only one node');
    is($let->{desc}, '..l');
    is_deeply($let->{template}, [qw<MJ_BAZ MJ_QUIX 0>]);
    my $par = compile_template(parse_sexp('(foo bar $1)'));
    is($par->{desc}, '..f');
    is_deeply($par->{template}, [qw<MJ_FOO MJ_BAR 1>]);
    my $subex = compile_template(parse_sexp('(foo bar (baz $1))'));
    is($subex->{root}, 2);
    is($subex->{desc}, '.f..l', 'Fill subexpression, link to parent');
    is_deeply($subex->{template}, [qw<MJ_BAZ 1 MJ_FOO MJ_BAR 0>]);
    my $complex_sexp = '(let (($foo (bar $1))) (foo zum $2 (zaf $foo 3)))';
    my $complex_expr = first(parse_sexp($complex_sexp));
    my $complex = compile_template($complex_expr);
    is ($complex->{root}, 5);
    is ($complex->{desc}, '.f.l...fl');
    is_deeply($complex->{template}, [qw(MJ_BAR 1 MJ_ZAF 0 3 MJ_FOO MJ_ZUM 2 2)]); 
} else { 
    # NYI
    print "Sorry, Full preprocessor is not yet implemented\n";
}
