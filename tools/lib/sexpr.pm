package sexpr;
use strict;
use warnings;
use Exporter qw(import);
our @EXPORT = qw(sexpr_decode sexpr_encode);

{
    # good thing perl is single threaded ;-)
    my $PARSER = __PACKAGE__->parser;
    sub sexpr_decode {
        open local $PARSER->{input}, '<', \$_[0];
        $PARSER->parse;
    }
}

sub sexpr_encode {
    my $list = shift;
    return "'$list'" unless ref($list);
    my $out = '(';
    for my $item (@$list) {
        if (ref($item) eq 'ARRAY') {
            $out .= sexpr_encode($item);
        } else {
            $out .= "$item";
        }
        $out .= " ";
    }
    $out = substr $out, 0, -1 if (substr $out, -1 eq ' ');
    $out .=  ')';
    return $out;
}


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
    return \@expr;
}



1;
