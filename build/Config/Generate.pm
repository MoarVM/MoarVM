package Config::Generate;
use strict;
use warnings;

sub file {
    my $source = shift;
    my $target = shift;
    my %config = @_;

    open my $fh_s, '<', $source or die $!;
    open my $fh_t, '>', $target or die $!;

    while (<$fh_s>) {
        s/@(\w+)@/$config{$1}/g;
        s/(\w)\/(\w|\*)/$1\\$2/g if $target =~ /Makefile/ && $^O =~ /MSWin32/;
        print $fh_t $_;
    }

    close $fh_s;
    close $fh_t;
}

'Punk IPA';
