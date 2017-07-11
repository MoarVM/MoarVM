#!/usr/bin/env perl
use strict;
use warnings;
use Data::Dumper;

use Getopt::Long;

use FindBin;
use lib $FindBin::Bin;
use sexpr;
use expr_ops;

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

my $OPLIST = 'src/core/oplist';     # We need this for generating the lookup table
my $PREFIX = 'MVM_JIT_';            # Prefix of all nodes
my ($INPUT, $OUTPUT);
my $TESTING;
GetOptions(
    'prefix=s' => \$PREFIX,
    'input=s' => sub { open $INPUT, '<', $_[1] or die "Could not open $_[1]"; },
    'output=s' => sub { open $OUTPUT, '>', $_[1] or die "Could not open $_[1]"; }
);
$OUTPUT = \*STDOUT unless defined $OUTPUT;
# any file-like arguments left, that's our input
if (@ARGV && -f $ARGV[0]) {
    open $INPUT, '<', $ARGV[0] or die "Could not open $ARGV[0]: $!";
}
$INPUT = \*STDIN unless defined $INPUT;


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

sub validate_template {
    my $template = shift;
    my $node = $template->[0];
    if ($node eq 'let:') {
        my $defs = $template->[1];
        my @expr = @$template[2..$#$template];
        for my $def (@$defs) {
            validate_template($def->[1]);
        }
        validate_template($_) for grep ref($_) eq 'ARRAY', @expr;
        return;
    }

    die "Unknown node type $node" unless exists $EXPR_OPS{$node};
    my ($nchild, $narg) = @{$EXPR_OPS{$node}}{qw(num_childs num_args)};;
    my $offset = 1;
    if ($nchild < 0) {
        die "First child of variadic node should be a number" unless $template->[1] =~ m/^\d+$/;
        $nchild = $template->[1];
        $offset = 2;
    }
    unless (($offset+$nchild+$narg) == @$template) {
        my $txt = sexpr::encode($template);
        die "Node $txt is too short";
    }
    for (my $i = 0; $i < $nchild; $i++) {
        my $child = $template->[$offset+$i];
        if (ref($child) eq 'ARRAY' and substr($child->[0], 0, 1) ne '&') {
            validate_template($child);
        } elsif (substr($child, 0, 1) eq '$') {
            # OK!
        } else {
            my $txt = sexpr::encode($template);
            die "Child $i of $txt is not a expression";
        }
    }
    for (my  $i = 0; $i < $narg; $i++) {
        my $child = $template->[$offset+$nchild+$i];
        if (ref($child) eq 'ARRAY' and substr($child->[0], 0, 1) eq '&') {
            # OK
        } elsif (substr($child, 0, 1) ne '$') {
            # Also OK
        } else {
            my $txt = sexpr::encode($template);
            die "Child $i of $txt is not an argument";
        }
    }
}


sub write_template {
    my ($tree, $templ, $desc, $env) = @_;
    die "Can't deal with an empty tree" unless @$tree; # we need at least some nodes
    my $top = $tree->[0]; # get the first item, used for dispatch
    die "First parameter must be a bareword or macro" unless $top =~ m/^&?[a-z]\w*:?$/i;
    my (@items, @desc); # accumulate state
    if ($top eq 'let:') {
        # rewrite (let: (($name ($code))) ($code..)+)
        # into (do(v)?: $ndec + $ncode $decl+ $code+)
        my $env  = { %$env }; # copy env and shadow it
        my $decl = $tree->[1];
        my @expr = @$tree[2..$#$tree];

        # depening on last node result, start with DO or DOV (void)
        my $type = $EXPR_OPS{$expr[-1][0]}{'type'};
        my $list = [ $type eq 'VOID' ? 'DOV' : 'DO', @$decl + @expr ];
        # add declarations to template and to DO list
        for my $stmt (@$decl) {
            die "Let statement should hold 2 expressions, holds ".@$stmt unless @$stmt == 2;
            die "Variable name {$stmt->[0]} is invalid" unless $stmt->[0] =~ m/\$[a-z]\w*/i;
            die "Let statement expects an expression" unless ref($stmt->[1]) eq 'ARRAY';
            die "Redeclaration of '$stmt->[0]'" if defined($env->{$stmt->[0]});
            my ($child, $mode) = write_template($stmt->[1], $templ, $desc, $env);
            die "Let can only be used with simple expresions" unless $mode eq 'l';
            $env->{$stmt->[0]} = $child;
            # ensure the DO is compiled as I expect.
            push @$list, ['DISCARD', $stmt->[0]];
        }
        push @$list, @expr;
        return write_template($list, $templ, $desc, $env);
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
            # named variable (declared in let)
            die "Undefined variable '$item' used" unless exists $env->{$item};
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


    # first read the correct order of opcodes
my (@opcodes, %names);
open my $oplist, '<', $OPLIST or die "Could not open $OPLIST";
while (<$oplist>) {
    next unless (m/^\w+/);
    my $opcode = substr $_, 0, $+[0];
    push @opcodes, $opcode;
    $names{$opcode} = $#opcodes;

}
close $oplist;

# read input, which should use the expresison-list
# syntax. generate template info table and template array
my %info;
my @templates;
my $parser = sexpr->parser($INPUT);


while (my $tree = $parser->parse) {
    my $keyword = shift @$tree;
    if ($keyword eq 'macro:') {
        my $name = shift @$tree;
        $parser->decl_macro($name, $tree);
    } elsif ($keyword eq 'template:') {
        my $opcode   = shift @$tree;
        my $template = shift @$tree;
        my $flags    = 0;
        if (substr($opcode, -1) eq '!') {
            # destructive template
            $opcode = substr $opcode, 0, -1;
            $flags |= 1;
        }
        die "Opcode '$opcode' unknown" unless defined $names{$opcode};
        die "Opcode '$opcode' redefined" if defined $info{$opcode};
        # Validate template for consistency with expr.h node definitions
        validate_template($template);
        my $compiled = compile_template($template);
        my $idx = scalar(@templates); # template index into array is current array top
        $info{$opcode} = {
            idx => $idx,
            info => $compiled->{desc},
            root => $compiled->{root},
            len => length($compiled->{desc}),
            flags => $flags
        };

        push @templates, @{$compiled->{template}};
    } else {
        die "I don't know what to do with '$keyword' ";
    }
}
close $INPUT;

# write a c output header file.
print $OUTPUT <<"HEADER";
/* FILE AUTOGENERATED BY $0. DO NOT EDIT.
 * Defines tables for expression templates. */
HEADER
    my $i = 0;
    print $OUTPUT "static const MVMJitExprNode MVM_jit_expr_templates[] = {\n    ";
    for (@templates) {
        $i += length($_) + 2;
        if ($i > 75) {
            print $OUTPUT "\n    ";
            $i = length($_) + 2;
        }
        print $OUTPUT "$_,";
    }
    print $OUTPUT "\n};\n";
    print $OUTPUT "static const MVMJitExprTemplate MVM_jit_expr_template_info[] = {\n";
    for (@opcodes) {
        if (defined($info{$_})) {
            my $td = $info{$_};
            printf $OUTPUT '    { MVM_jit_expr_templates + %d, "%s", %d, %d, %d },%s',
                           $td->{idx}, $td->{info}, $td->{len}, $td->{root}, $td->{flags}, "\n";
        } else {
            print $OUTPUT "    { NULL, NULL, -1, 0 },\n";
        }
    }
    print $OUTPUT "};\n";
    print $OUTPUT <<'FOOTER';
static const MVMJitExprTemplate * MVM_jit_get_template_for_opcode(MVMuint16 opcode) {
    if (opcode >= MVM_OP_EXT_BASE) return NULL;
    if (MVM_jit_expr_template_info[opcode].len < 0) return NULL;
    return &MVM_jit_expr_template_info[opcode];
}
FOOTER

