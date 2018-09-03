#!/usr/bin/env perl
package template_compiler;
use strict;
use warnings FATAL => 'all';

use Getopt::Long;
use File::Spec;
use Scalar::Util qw(looks_like_number refaddr reftype);

# use my libs
use FindBin;
use lib $FindBin::Bin;

use sexpr;
use expr_ops;
use oplist;



# Input:
#   (load (addr pargs $1))
# Output
#   template: (MVM_JIT_ADDR, MVM_JIT_PARGS, 1, MVM_JIT_LOAD, 0)
#   length: 5, root: 3 "..f..l"


# options to compile
my %OPTIONS = (
    prefix => 'MVM_JIT_',
    oplist => File::Spec->catfile($FindBin::Bin, File::Spec->updir, qw(src core oplist)),
    include => 1,
);
GetOptions(\%OPTIONS, qw(prefix=s list=s input=s output=s include!));

my ($PREFIX, $OPLIST) = @OPTIONS{'prefix', 'oplist'};
if ($OPTIONS{output}) {
    close( STDOUT ) or die $!;
    open( STDOUT, '>', $OPTIONS{output} ) or die $!;
}

if ($OPTIONS{input} //= shift @ARGV) {
    close( STDIN );
    open( STDIN, '<', $OPTIONS{input} ) or die $!;
}

END {
    close STDOUT;
    if ($? && $OPTIONS{output}) {
        unlink $OPTIONS{output};
    }
}

# Template check tables

# Expected result type
my %OPERATOR_TYPES = (
    (map { $_ => 'void' } qw(store discard dov when ifv branch mark callv guard)),
    (map { $_ => 'flag' } qw(lt le eq ne ge gt nz zr all any)),
    qw(arglist) x 2,
    qw(carg) x 2,
);


# Expected type of operands
my %OPERAND_TYPES = (
    flagval => 'flag',
    all => 'flag',
    any => 'flag',
    do => 'void,reg',
    dov => 'void',
    when => 'flag,void',
    if => 'flag,reg,reg',
    ifv => 'flag,void,void',
    call => 'reg,arglist',
    callv => 'reg,arglist',
    arglist => 'carg',
    guard => 'void',
);

# which list item is the size
my %OP_SIZE_PARAM = (
    load => 2,
    store => 3,
    call => 3,
    const => 2,
    cast => 2,
);

my %VARIADIC = map { $_ => 1 } grep $EXPR_OPS{$_}{num_operands} < 0, keys %EXPR_OPS;

# first read the correct order of opcodes
my %OPNAMES = map { $OPLIST[$_][0] => $_ } 0..$#OPLIST;

# cache operand direction
sub opcode_operand_direction {
    $_[0] =~ m/^([rw])l?\(/ ? $1 : '';
}

# Pre compute exptected operand type and direction
my %MOAR_OPERAND_DIRECTION;
for my $opcode (@OPLIST) {
    my ($opname, undef, $operands) = @$opcode;
    $MOAR_OPERAND_DIRECTION{$opname} = [
        map opcode_operand_direction($_), @$operands
    ];
}

# Need a global constant table
my %CONSTANTS;

sub compile_template {
    my ($expr, $opcode) = @_;
    my $compiler = +{
        types => {},
        env   => {},
        expr  => {},
        tmpl  => [],
        desc  => [],
        opcode => $opcode,
        constants => \%CONSTANTS,
    };
    my ($mode, $root) = compile_expression($compiler, $expr);
    die "Invalid template!" unless $mode eq 'l'; # top should be a simple expression
    return {
        root => $root,
        template => $compiler->{tmpl},
        desc => join('', @{$compiler->{desc}}),
    };
}

sub is_arrayref {
    defined(reftype($_[0])) && reftype($_[0]) eq 'ARRAY';
}

sub apply_macros {
    my ($expr, $macros) = @_;
    return unless is_arrayref($expr);

    my @result;
    for my $element (@$expr) {
        if (is_arrayref($element)) {
            push @result, apply_macros($element, $macros);
        } else {
            push @result, $element;
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
            die "Tried to instantiate undefined macro $name";
        }
    }
    return \@result;
}

sub fill_macro {
    my ($macro, $bind) = @_;
    my $result = [];
    for my $element (@$macro) {
        if (is_arrayref($element)) {
            push @$result, fill_macro($element, $bind);
        } elsif ($element =~ m/^,/) {
            if (defined $bind->{$element}) {
                push @$result, $bind->{$element};
            } else {
                die "Unmatched macro substitution: $element";
            }
        } else {
            push @$result, $element;
        }
    }
    return $result;
}

sub check_type {
    my ($compiler, $expr) = @_;
    if (is_arrayref($expr)) {
        my ($operator) = @$expr;
        die "Expected operator but got macro" if $operator =~ m/^&/;
        return check_type($compiler, $expr->[$#$expr]) if $operator eq 'let:';
        return $OPERATOR_TYPES{$operator} || 'reg';
    } elsif ($expr =~ m/^\\?\$(\w+)$/) {
        my $name = $1;
        if (looks_like_number($name)) {
            # TODO - we can do much better, but it is not easy,
            # because there are a number of opcodes like 'bindlex'
            # which introduce wildcards, and a number of operators
            # (like COPY and STORE) which are generic.
            return 'reg';
        } else {
            die "$name is not declared" unless exists $compiler->{types}{$expr};
            return $compiler->{types}{$expr};
        }
    } else {
        die "Expected operator or type, got " . sexpr::encode($expr);
    }
}



sub compile_expression {
    my ($compiler, $expr) = @_;

    return 'l' => $compiler->{expr}{refaddr($expr)}
        if exists $compiler->{expr}{refaddr($expr)};

    my ($operator, @operands) = @$expr;

    return compile_declarations($compiler, @operands)
        if $operator eq 'let:';

    die "Expected expression but got macro" if $operator =~ m/^&/;
    die "Unknown operator $operator" unless my $info = $EXPR_OPS{$operator};

    my $num_operands = $VARIADIC{$operator} ? @operands : $info->{num_operands};
    my $num_params = $info->{num_params};

    die "Expected $num_operands operands and $num_params params, got " . 0+@operands
        if $num_operands + $num_params != @operands;

    # large constants are treated specially
    if ($operator =~ m/^const_(ptr|large)$/) {
        my ($value, $size) = @operands;
        return 'l' => emit($compiler,
                           compile_operator($compiler, $operator, 0),
                           compile_constant($compiler, $value),
                           defined $size ? ('.' => $size) : ());
    }

    # match up types
    my @types = split /,/, ($OPERAND_TYPES{$operator} // 'reg');
    if (@types < $num_operands) {
        if (@types == 1) {
            @types = (@types) x $num_operands;
        } elsif (@types == 2) {
            @types = (($types[0]) x ($num_operands-1), $types[1]);
        } else {
            die "Can't match up types";
        }
    }

    my @code = compile_operator($compiler, $operator, $num_operands);

    my $i = 0;
    for (; $i < $num_operands; $i++) {
        my $type = check_type($compiler, $operands[$i]);
        die "Mismatched type, got $type expected $types[$i]" .
            "for $operator ($compiler->{opcode} $operands[$i])"
            unless $type eq $types[$i];
        push @code, compile_operand($compiler, $operands[$i]);
    }

    # check size parameter if any
    if (my $param = $OP_SIZE_PARAM{$operator}) {
        my $size = $operands[$param - 1];
        die "Expected size parameter" unless
            # macro, number or bareword-ending-with-size
            ((is_arrayref($size) && $size->[0] =~ m/^&/) ||
             looks_like_number($size) || $size =~ m/_sz$/);
    }
    for (; $i < $num_operands + $num_params; $i++) {
        push @code, compile_parameter($compiler, $operands[$i]);
    }
    my $node = emit($compiler, @code);
    $compiler->{expr}{refaddr($expr)} = $node;
    return 'l' => $node;
}

sub compile_declarations {
    my ($compiler, $declarations, @rest) = @_;
    # rewrite (let: (($name ($code))) ($code..)+)
    # into (do(v)?: $ndec + $ncode $decl+ $code+)
    my $type = check_type($compiler, $rest[$#rest]);
    my @list = ($type eq 'void' ? 'dov' : 'do');

    # Localize scope to subexpressions only
    local $compiler->{env} = +{ %{$compiler->{env}} };
    local $compiler->{types} = +{ %{$compiler->{types}} };

    for my $declaration (@$declarations) {
        die "Need name and expression" unless @$declaration == 2;
        my ($name, $expr) = @$declaration;
        die "Variable name $name is invalid" unless $name =~ m/\$[a-z]\w*/i;
        # Not safe because of macros
        die "Illegal redeclaration of $name" if exists $compiler->{env}{$name};

        my $type = check_type($compiler, $expr);
        die "Let declaration needs a register value got $type" unless $type eq 'reg';

        (undef, my $node) = compile_expression($compiler, $expr);
        # permit 'forward' declarations within same let scope
        $compiler->{types}{$name} = $type;
        $compiler->{env}{$name} = $node;

        push @list, ['discard', $name];
    }
    push @list, @rest;
    return compile_expression($compiler, \@list);
}

sub compile_constant {
    my ($compiler, $value, $size) = @_;
    my $constants = $compiler->{constants};
    my $const_nr = ($constants->{$value} = exists $constants->{$value} ?
                        $constants->{$value} : scalar keys %$constants);
    return 'c' => $const_nr;
}

sub compile_operand {
    my ($compiler, $expr) = @_;
    if (is_arrayref($expr)) {
        compile_expression($compiler, $expr);
    } else {
        compile_reference($compiler, $expr);
    }
}

sub compile_reference {
    my ($compiler, $expr) = @_;
    die "Expected reference got $expr" unless
        my ($ref, $name) = $expr =~ m/^(\\?)\$(\w+)/;
    if (looks_like_number($name)) {
        my $opcode = $compiler->{opcode};
        # special case for dec_i/inc_i
        return 'i' => $name if $opcode =~ m/^(dec|inc)_i$/ and $name <= 1;
        my $direction = $MOAR_OPERAND_DIRECTION{$opcode};
        die "Invalid operand reference $expr for $opcode"
            unless $name >= 0 && $name < @$direction;
        die "Expected reference for write operand ($opcode)"
            if $direction->[$name] eq 'w' && !$ref;
        return 'i' => $name;
    } else {
        die "Undefined named reference $expr"
            unless defined (my $ref = $compiler->{env}{$expr});
        return 'l' => $ref;
    }
}

sub compile_parameter {
    my ($compiler, $expr) = @_;
    if (is_arrayref($expr)) {
        return compile_macro($compiler, $expr);
    } elsif (looks_like_number($expr)) {
        return '.' => $expr;
    } else {
        return compile_bareword($compiler, $expr);
    }
}

sub compile_macro {
    my ($compiler, $expr) = @_;
    my ($name, @parameters) = @$expr;
    die "Expected a macro expression, got $name"
        unless my ($macro) = $name =~ m/^&(\w+)/;
    return '.' => sprintf('%s(%s)', $macro, join(', ', @parameters));
}

sub compile_operator {
    my ($compiler, $expr, $num_operands) = @_;
    die "$expr is not a valid operator" unless exists $EXPR_OPS{$expr};
    die "Invalid size $num_operands" unless looks_like_number($num_operands);
    return ('n' => $PREFIX . uc($expr), 's' => $num_operands);
}

sub compile_bareword {
    my ($compiler, $expr) = @_;
    return '.' => $PREFIX . uc($expr);
}

sub emit {
    my ($compiler, @code) = @_;
    my $node = @{$compiler->{tmpl}};
    while (@code) {
        push @{$compiler->{desc}}, shift @code;
        push @{$compiler->{tmpl}}, shift @code;
    }
    return $node;
}



my %SEEN;

sub parse_file {
    my ($fh, $macros) = @_;
    my (@templates, %info);
    my $parser = sexpr->parser($fh);
    while (my $raw = $parser->parse) {
        my $tree    = apply_macros($raw, $macros);
        my $keyword = shift @$tree;
        if ($keyword eq 'macro:') {
            my $name = shift @$tree;
            $macros->{$name} = $tree;
        } elsif ($keyword eq 'template:') {
            my $opcode   = shift @$tree;
            my $template = shift @$tree;
            my $flags    = 0;
            if ($opcode =~ s/!$//) {
                die "No write operand for destructive template $opcode"
                    unless grep $_ eq 'w', @{$MOAR_OPERAND_DIRECTION{$opcode}};
                $flags |= 1;
            }
            die "Opcode '$opcode' unknown" unless defined $OPNAMES{$opcode};
            die "Opcode '$opcode' redefined" if defined $info{$opcode};

            my $compiled = compile_template($template, $opcode);

            $info{$opcode} = {
                idx => scalar @templates,
                info => $compiled->{desc},
                root => $compiled->{root},
                len => length($compiled->{desc}),
                flags => $flags
            };
            push @templates, @{$compiled->{template}};
        } elsif ($keyword eq 'include:') {
            my $file = shift @$tree;
            $file =~ s/^"|"$//g;

            if ($SEEN{$file}++) {
                warn "$file already included";
                next;
            }

            open( my $handle, '<', $file ) or die $!;
            my ($inc_templates, $inc_info) = parse_file($handle, $macros);
            close( $handle ) or die $!;
            die "Template redeclared in include" if grep $info{$_}, keys %$inc_info;

            # merge templates into including file
            $_->{idx} += @templates for values %$inc_info;
            $info{keys %$inc_info} = values %$inc_info;
            push @templates, @$inc_templates;

        } else {
            die "I don't know what to do with '$keyword' ";
        }
    }
    return \(@templates, %info);
}


my ($templates, $info) = parse_file(\*STDIN, {});
close( STDIN ) or die $!;

# write a c output header file.
print <<"HEADER";
/* FILE AUTOGENERATED BY $0. DO NOT EDIT.
 * Defines tables for expression templates. */
HEADER
my $i = 0;
print "static const MVMint32 MVM_jit_expr_templates[] = {\n    ";
for (@$templates) {
    $i += length($_) + 2;
    if ($i > 75) {
        print "\n    ";
        $i = length($_) + 2;
    }
    print "$_,";
}
print "\n};\n";
print "static const MVMJitExprTemplate MVM_jit_expr_template_info[] = {\n";
for my $opcode (@OPLIST) {
    my ($name) = @$opcode;
    if (defined($info->{$name})) {
        my $td = $info->{$name};
        printf '    { MVM_jit_expr_templates + %d, "%s", %d, %d, %d },%s',
          $td->{idx}, $td->{info}, $td->{len}, $td->{root}, $td->{flags}, "\n";
    } else {
        print "    { NULL, NULL, -1, 0 },\n";
    }
}
print "};\n";

my @constants; @constants[values %CONSTANTS] = keys %CONSTANTS;
print "static const void* MVM_jit_expr_template_constants[] = {\n";
print "    $_,\n" for @constants;
print "};\n";

printf <<'FOOTER', scalar @OPLIST;
static const MVMJitExprTemplate * MVM_jit_get_template_for_opcode(MVMuint16 opcode) {
    if (opcode >= %d) return NULL;
    if (MVM_jit_expr_template_info[opcode].len < 0) return NULL;
    return &MVM_jit_expr_template_info[opcode];
}
FOOTER
