#!/usr/bin/env perl
use strict;
use warnings;
# use very strict
use autodie qw(open close);
use warnings FATAL => 'all';

use Getopt::Long;
use File::Spec;
# use my libs
use FindBin;
use lib $FindBin::Bin;

use sexpr;
use expr_ops;


# Input:
#   (load (addr pargs $1))
# Output
#   template: (MVM_JIT_ADDR, MVM_JIT_PARGS, 1, MVM_JIT_LOAD, 0)
#   length: 5, root: 3 "..f..l"


# options to compile
my %OPTIONS = (
    prefix => 'MVM_JIT_',
    oplist => File::Spec->catfile($FindBin::Bin, File::Spec->updir, qw(src core oplist)),
);
GetOptions(\%OPTIONS, qw(prefix=s list=s input=s output=s));

my ($PREFIX, $OPLIST) = @OPTIONS{'prefix', 'oplist'};
if ($OPTIONS{output}) {
    close STDOUT;
    open STDOUT, '>', $OPTIONS{output};
}
if ($OPTIONS{input} //= shift @ARGV) {
    close STDIN;
    open STDIN, '<', $OPTIONS{input};
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

sub apply_macros {
    my ($tree, $macros) = @_;
    return unless ref($tree) eq 'ARRAY';

    my @result;
    for my $node (@$tree) {
        if (ref($node) eq 'ARRAY') {
            push @result, apply_macros($node, $macros);
        } else {
            push @result, $node;
        }
    }
    # empty lists can occur for instance with macros without arguments
    if (@result and $result[0] =~ m/^\^/) {
        # looks like a macro
        my $name = shift @result;
        if (my $macro = $macros->{$name}) {
            my ($params, $structure) = @$macro[0,1];
            die sprintf("Macro %s needs %d params, got %d", $name, 0+@result, 0+@{$params})
                unless @result == @{$params};
            my %bind; @bind{@$params} = @result;
            return fill_macro($structure, \%bind);
        } else {
            die "Tried to instantiate undefined macro $result[0]";
        }
    }
    return \@result;
}

sub fill_macro {
    my ($macro, $bind) = @_;
    my $result = [];
    for (my $i = 0; $i < @$macro; $i++) {
        if (ref($macro->[$i]) eq 'ARRAY') {
            push @$result, fill_macro($macro->[$i], $bind);
        } elsif (substr($macro->[$i], 0, 1) eq ',') {
            if (defined $bind->{$macro->[$i]}) {
                push @$result, $bind->{$macro->[$i]};
            } else {
                die "Unmatched macro substitution: $macro->[$i]";
            }
        } else {
            push @$result, $macro->[$i];
        }
    }
    return $result;
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
{
    open my $oplist, '<', $OPLIST;
    while (<$oplist>) {
        next unless (m/^\w+/);
        my $opcode = substr $_, 0, $+[0];
        push @opcodes, $opcode;
        $names{$opcode} = $#opcodes;
    }
    close $oplist;
}

# read input, which should use the expresison-list
# syntax. generate template info table and template array
my %macros;
my %info;
my @templates;
my $parser = sexpr->parser(\*STDIN);
while (my $tree = apply_macros($parser->parse, \%macros)) {
    my $keyword = shift @$tree;
    if ($keyword eq 'macro:') {
        my $name = shift @$tree;
        # declare that macro
        $macros{$name} = $tree;
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
close STDIN;

# write a c output header file.
print <<"HEADER";
/* FILE AUTOGENERATED BY $0. DO NOT EDIT.
 * Defines tables for expression templates. */
HEADER
    my $i = 0;
    print "static const MVMJitExprNode MVM_jit_expr_templates[] = {\n    ";
    for (@templates) {
        $i += length($_) + 2;
        if ($i > 75) {
            print "\n    ";
            $i = length($_) + 2;
        }
        print "$_,";
    }
    print "\n};\n";
    print "static const MVMJitExprTemplate MVM_jit_expr_template_info[] = {\n";
    for (@opcodes) {
        if (defined($info{$_})) {
            my $td = $info{$_};
            printf '    { MVM_jit_expr_templates + %d, "%s", %d, %d, %d },%s',
                           $td->{idx}, $td->{info}, $td->{len}, $td->{root}, $td->{flags}, "\n";
        } else {
            print "    { NULL, NULL, -1, 0 },\n";
        }
    }
    print "};\n";
    print <<'FOOTER';
static const MVMJitExprTemplate * MVM_jit_get_template_for_opcode(MVMuint16 opcode) {
    if (opcode >= MVM_OP_EXT_BASE) return NULL;
    if (MVM_jit_expr_template_info[opcode].len < 0) return NULL;
    return &MVM_jit_expr_template_info[opcode];
}
FOOTER

