package sexpr;
use strict;

# declare keyword syntax regex
my $keyword = qr/^[&\$^,]?[\w\.\[\]_\*]+[!:]?/;

sub parser {
    my ($class, $input) = @_;
    return bless {
        input => $input,
        buffer => '',
        macros => {},
    }, $class;
}

sub read {
    my $self = shift;
    my $file = $self->{input};
    my $expr = $self->{buffer};
    my ($open, $close) = (0, 0);
    while (!eof($file)) {
        my $line = <$file>;
        next if $line =~ m/^#|^\s*$/;
        $expr  .= $line;
        $open   = $expr =~ tr/(//;
        $close  = $expr =~ tr/)//;
        last if ($open > 0) && ($open == $close);
    }
    die "End of input with unclosed template" if $open > $close;
    my ($tree, $rest) = $self->parse($expr);
    $self->{buffer} = $rest;
    return $tree;
}

sub parse {
    my ($self, $expr) = @_;
    my $tree = [];
    # consume initial opening parenthesis
    return (undef, $expr) unless $expr =~ m/^\s*\(/;
    $expr = substr($expr, $+[0]);
    while ($expr) {
        # remove initial space
        $expr =~ s/^\s*//;
        if (substr($expr, 0, 1) eq '(') {
            # descend on opening parenthesis
            my ($child, $rest) = $self->parse($expr);
            $expr = $rest;
            push @$tree, $child;
        } elsif (substr($expr, 0, 1) eq ')') {
            # ascend on closing parenthesis
            $expr = substr $expr, 1;
            last;
        } elsif ($expr =~ m/$keyword/) {
            # consume keyword
            push @$tree, substr($expr, $-[0], $+[0] - $-[0]);
            $expr = substr $expr, $+[0];
        } else {
            die "Could not parse $expr";
        }
    }
    if (@$tree && substr($tree->[0], 0, 1) eq '^') {
        if (defined $self->{macros}->{$tree->[0]}) {
            $tree = apply_macro($self->{macros}->{$tree->[0]}, $tree);
        } else {
            die "Attempted to invoke undefined macro $tree->[0]";
        }
    }
    return ($tree, $expr);
}

sub decl_macro {
    my ($self, $name, $macro) = @_;
    die "Macro name '$name' must start with ^ symbol" unless substr($name,0,1) eq '^';
    die "Redeclaration of macro $name" if defined $self->{'macros'}->{$name};

    $self->{macros}->{$name} = $macro;
}

sub apply_macro {
    my ($macro, $tree) = @_;
    my $params = $macro->[0];
    my $args   = [@$tree[1..$#$tree]];
    die "Incorrect number of args, got ".@$args." expected ".@$params unless @$args == @$params;
    my %bind;
    @bind{@$params} = @$args;
    return fill_macro($macro->[1], \%bind);
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

sub encode {
    my $list = shift;
    my $out = '(';
    for my $item (@$list) {
        if (ref($item) eq 'ARRAY') {
            $out .= encode($item);
        } else {
            $out .= "$item";
        }
        $out .= " ";
    }
    $out   .= ')';
    return $out;
}

1;
