package sexpr;
use strict;
use warnings;

# declare keyword syntax regex
my $tokenize = qr/
    \A
    (?<open>\() |
    (?<close>\)) |
    (?<space>\s+) |
    (?<comment>\#.+) |
    (?<string>\".*?") |
    (?<word>[^\s\(\)\#"']+)
/x;

sub parser {
    my ($class, $input) = @_;
    return bless {
        input => $input,
        buffer => '',
        token => undef,
        match => undef,
        macros => {},
    }, $class;
}

sub empty {
    my $self = shift;
    length($self->{buffer}) == 0 and eof($self->{input});
}

sub current {
    my $self = shift;
    unless (length($self->{buffer}) or eof($self->{input})) {
        $self->{buffer} = readline($self->{input});
    }
    $self->{buffer};
}


sub token {
    my $self = shift;
    my $line = $self->current;
    # cache token
    return @$self{'token','match'} if $self->{token};
    return unless length($line);
    return unless $line =~ $tokenize;
    @$self{'token','match'} = %+;
}

sub _shift {
    my ($self) = @_;
    my $length = length($self->{match});
    @$self{'token','match'} = (undef,undef);
    substr($self->{buffer}, 0, $length, '');
}

sub expect {
    my ($self, $expect) = @_;
    my ($token, $match) = $self->token;
    die "Got $token but expected $expect" unless $expect eq $token;
    $self->_shift;
}

sub peek {
    my ($self, $expect) = @_;
    my ($token, $match) = $self->token or return;
    return $match if $token eq $expect;
}

sub skip {
    my ($self, @possible) = @_;
    my %check = map { $_ => 1 } @possible;
    while (my ($token, $match) = $self->token) {
        last unless $check{$token};
        $self->_shift;
    }
}

sub parse {
    my $self = shift;
    $self->skip('comment', 'space');
    return if $self->empty;
    $self->expect('open');
    my @expr;
    until ($self->peek('close')) {
        die "Could not continue reading" if $self->empty;
        my ($token, $what) = $self->token or
            die "Could not read a token";
        if ($token eq 'word' or $token eq 'string') {
            push @expr, $self->_shift;
        } elsif ($token eq 'open')  {
            push @expr, $self->parse;
        } else {
            $self->_shift;
        }
    }
    $self->_shift;
    if (@expr and $expr[0] =~ m/\A\^/) {
        my $macro = $self->{macros}{$expr[0]}; 
        if (defined $macro) {
            @expr = @{apply_macro($macro, \@expr)};
        } else {
            die "Attempt to invoke undefined macro by name: $expr[0]";
        }
    }
    return \@expr;
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
    $out = substr $out, 0, -1 if (substr $out, -1 eq ' ');
    $out .=  ')';
    return $out;
}

1;
