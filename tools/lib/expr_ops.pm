package expr_ops;
use strict;
use warnings;
use File::Spec::Functions qw(catdir updir catpath splitpath);
use constant EXPR_OPS_H => do {
    my ($path, $directory, $filename) = splitpath(__FILE__);
    catpath($path, catdir($directory, (updir()) x 2, 'src','jit'), 'expr_ops.h');
};

sub parse_expr_ops {
    my ($file_name) = @_;
    open my $fh, '<', $file_name
      or die "Coulqd not open '$file_name': $!";
    while (<$fh>) { last if m/#define MVM_JIT_EXPR_OPS/; }
    my @expr_ops;
    while (<$fh>) {
        next unless (m/\((\w+),\s*(-?\d+),\s*(\d+)\)/);
        push @expr_ops, [$1, $2, $3];
    }
    close $fh;
    return @expr_ops;
}

sub import {
    my ($class, @args) = @_;
    my @keys     = qw(name num_operands num_params cast);
    my @expr_ops = parse_expr_ops(@args ? @args : EXPR_OPS_H);
    my %expr_ops = map {
        my ($i, $op) = ($_, $expr_ops[$_]);
        lc($op->[0]) => { map({ $keys[$_] => $op->[$_] } 1..$#$op), 'idx' => $i };
    } 0..$#expr_ops;
    my ($caller) = caller();
    {
        no strict 'refs';
        *{$caller . '::EXPR_OPS'} = \@expr_ops;
        *{$caller . '::EXPR_OPS'} = \%expr_ops;
    }
}

1;
