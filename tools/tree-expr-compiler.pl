#!/usr/bin/env perl
use Test::More;
use Getopt::Long;
use strict;
use warnings;

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
#   template: (MVM_JIT_ADDR, MVM_JIT_PARGS, 1, MVM_JIT_LOAD, 0)
#   length: 5, root: 3 "..f..l"

my $OPLIST = 'src/core/oplist'; # We need this for generating the lookup table
my $PREFIX = 'MVM_JIT_';       # Prefix of all ndoes
my ($INPUT, $OUTPUT);
my $TESTING;
GetOptions(
    'test' => \$TESTING,
    'prefix=s' => \$PREFIX,
    'input=s' => sub { open $INPUT, '<', $_[1] or die "Could not open $_[1]"; },
    'output=s' => sub { open $OUTPUT, '>', $_[1] or die "Could not open $_[1]"; }
);
$OUTPUT = \*STDOUT unless defined $OUTPUT;
# any file-like arguments left, that's our input
if (@ARGV && -f $ARGV[0]) {
    open $INPUT, '<', $ARGV[0] or die "Could not open $ARGV[0]";
}
$INPUT = \*STDIN unless defined $INPUT;



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
        } elsif ($expr =~ m/^[&\$]?[\w\.\[\]_\*]+/) {
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



sub write_template {
    my ($tree, $templ, $desc, $env) = @_;
    die "Can't deal with an empty tree" unless @$tree; # we need at least some nodes
    my $top = $tree->[0]; # get the first item, used for dispatch
    die "First parameter must be a bareword or macro" unless $top =~ m/^&?[a-z]\w*$/i;
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
    } elsif (substr($top, 0, 1) eq '&') {
        # Add macro or sizeof/offsetof expression. these are not
        # processed in at runtime! Must evaluate to constant
        # expression.
        return (sprintf('%s(%s)', substr($top, 1), 
                        join(', ', @$tree[1..$#$tree])), '.');
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
            push @items, $PREFIX . uc($item);
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



if ($TESTING) {
    $PREFIX = 'MJ_';
    sub check_parse {
        my ($exp, $expected, $msg) = @_;
        my ($parsed, $rest) = parse_sexp($exp);
        is_deeply($parsed, $expected, $msg);
    }
    check_parse('', undef, 'empty string should parse to undef');
    check_parse('()', [], 'empty parens should be empty list');
    check_parse('(foo)', ['foo'], 'single item list');
    check_parse('(foo bar)', [qw<foo bar>]);
    check_parse('(foo (bar))', ['foo', ['bar']]);
    check_parse('((foo) (bar))', [['foo'], ['bar']]);
    check_parse('(0)', ['0']);
    eval { compile_template(parse_sexp('()')) }; ok $@, 'Cannot compile empty template';
    eval { compile_template(parse_sexp('(foo bar)')) }; ok !$@, 'a simple expression should work';
    eval { compile_template(parse_sexp('(&offsetof foo bar)')) };
    ok $@, 'Template root must be simple expr';
    eval { compile_template(parse_sexp('(foo (&sizeof 1))')) }; ok !$@, 'use sizeof as a macro';
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
    my $complex_sexp = '(let (($foo (bar $1))) (foo zum $2 (zaf $foo 3) (&sizeof int)))';
    my ($complex_expr, $rest) = parse_sexp($complex_sexp);
    my $complex = compile_template($complex_expr);
    is ($complex->{root}, 5);
    is ($complex->{desc}, '.f.l...fl.');
    is_deeply($complex->{template}, [qw(MJ_BAR 1 MJ_ZAF 0 3 MJ_FOO MJ_ZUM 2 2 sizeof(int))]);
    done_testing();
} else {
    # first read the correct order of opcodes
    my (@opcodes, %names);
    open my $oplist, '<', $OPLIST;
    while (<$oplist>) {
        next unless (m/^\w+/);
        my $opcode = substr $_, 0, $+[0];
        $names{$opcode} = scalar(@opcodes);
        push @opcodes, $opcode;
    }
    close $oplist;

    # read input, which should use the expresison-list
    # syntax. generate template info table and template array
    my @LINES = <$INPUT>;
    my %info;
    my @templates;
    while (@LINES) {
        my $line = shift @LINES;
        # skip empty lines and comments
        next if $line =~ m/^#|^\s*$/;
        die "Opcode not parsed" unless $line =~ m/^\w+:/;
        my $opcode = substr($line, 0, $+[0]-1);
        die "Opcode unknown" unless defined $names{$opcode};
        my $expr   = substr($line, $+[0]);
        # count parentheses
        my $open   = $expr =~ tr/(//;
        my $close  = $expr =~ tr/)//;
        while ($open > $close) {
            die "End of input with unclosed tree template" unless @LINES;
            $line = shift @LINES;
            die "Opcode in unclosed tree template" if $line =~ m/^\w+:/;
            $expr  .= $line;
            $open  += $line =~ tr/(//;
            $close += $line =~ tr/)//;
        }
        my ($parsed, $rest) = parse_sexp($expr);
        my $compiled = compile_template($parsed);
        my $idx = scalar(@templates); # template index into array is current array top
        $info{$opcode} = { idx => $idx, info => $compiled->{desc},
                           root => $compiled->{root},
                           len => length($compiled->{desc}) };
        push @templates, @{$compiled->{template}};
    }

    # write a c output header file.
    print $OUTPUT <<'HEADER';
/* AUTOGENERATED FILE. DO NOT EDIT.         *
 * Defines tables for expression templates. */
typedef struct {
    const MVMint64 *template;
    const char *info;
    MVMint32 len;
    MVMint32 root;
} MVMJitExprTemplateInfo;
HEADER
    my $i = 0;
    print $OUTPUT "static const MVMint64 MVM_jit_expr_templates[] = {\n    ";
    for (@templates) {
        $i += length($_) + 2;
        if ($i > 75) {
            print $OUTPUT "\n    ";
            $i = length($_) + 2;
        }
        print $OUTPUT "$_, ";
    }
    print $OUTPUT "\n};\n";
    print $OUTPUT "static const MVMJitExprTemplateInfo MVM_jit_expr_template_info[] = {\n";
    for (@opcodes) {
        if (defined($info{$_})) {
            my $td = $info{$_};
            print $OUTPUT "    { &MVM_jit_expr_templates[$td->{idx}], \"$td->{info}\", $td->{len}, $td->{root} },\n";
        } else {
            print $OUTPUT "    { NULL NULL, -1, 0 },\n";
        }
    }
    print $OUTPUT "};\n";
    print $OUTPUT <<'FOOTER';
static MVMJitExprTemplateInfo * MVM_jit_get_template_for_opcode(MVMint16 opcode) {
    if (opcode >= MVM_OP_EXT_BASE) return NULL;
    if (MVM_jit_expr_template_info[opcode].root < 0) return NULL;
    return &MVM_jit_expr_template_info[opcode];
}
FOOTER
}
