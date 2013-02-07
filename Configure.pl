#!perl

use 5.010;
use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;

use lib 'build';
use Config::BuildEnvironment;
use Config::APR;
use Config::LAO;
use Config::Generate;


my %opts;
GetOptions(\%opts, 'help|?', 'debug!', 'optimize!', 'instrument!');
pod2usage(1) if $opts{help};

print "Welcome to MoarVM!\n\n";

print dots("Checking master build settings ...");
$opts{debug}      //= 0 + !$opts{optimize};
$opts{optimize}   //= 0 + !$opts{debug};
$opts{instrument} //= 0;
print " OK\n    (debug: " . _yn($opts{debug})
    . ", optimize: "      . _yn($opts{optimize})
    . ", instrument: "    . _yn($opts{instrument}) . ")\n";

print dots("Trying to figure out how to build on your system ...");
my %config = Config::BuildEnvironment::detect(\%opts);
if (!$config{'excuse'}) {
    print " OK\n    (OS: $config{'os'}, Compiler: $config{'cc'}, Linker: $config{'link'})\n";
}
else {
    print " FAILED\n";
    die "    Sorry, I'm not sure how to build on this platform:\n    $config{'excuse'}\n";
}

print dots("Configuring APR ...");
%config = Config::APR::configure(%config);
check_excuse();

print dots("Configuring libatomic_ops ...");
%config = Config::LAO::configure(%config);
check_excuse();

print dots("Generating config.h ...");
Config::Generate::file('build/config.h.in', 'src/gen/config.h', %config);
print " OK\n";

print dots("Generating Makefile ...");
Config::Generate::file('build/Makefile.in', 'Makefile', %config);
print " OK\n";

print "\nConfiguration successful. Type '$config{'make'}' to build.\n";


sub dots {
    my $message = shift;
    my $length  = shift || 55;

    return $message . '.' x ($length - length $message);
}

sub _yn {
    return $_[0] ? 'YES' : 'no';
}

sub check_excuse {
    if (!$config{'excuse'}) {
        print " OK\n";
    }
    else {
        print " FAILED\n";
        die "    $config{'excuse'}\n";
    }
}

__END__

=head1 SYNOPSIS

    ./Configure.pl [-?|--help]
    ./Configure.pl [--debug] [--optimize] [--instrument]

=head1 OPTIONS

=over 4

Except for C<-?|--help>, any option can be explicitly turned off by
preceding it with C<no->, as in C<--no-optimize>.

=item -?|--help

Show this help information.

=item --debug

Turn on debugging flags during compile and link.  If C<--optimize> is not
explicitly set, debug defaults to on, and optimize defaults to off.

=item --optimize

Turn on optimization flags during compile and link.  If C<--debug> is not
explicitly set, turning this on defaults debugging off; otherwise this
defaults to the opposite of C<--debug>.

=item --instrument

Turn on extra instrumentation flags during compile and link; for example,
turns on Address Sanitizer when compiling with F<clang>.  Defaults to off.

=back
