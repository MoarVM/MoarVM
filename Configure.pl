#!perl
use strict;
use warnings;
use lib 'build';
use Config::BuildEnvironment;
use Config::APR;
use Config::LAO;
use Config::Generate;

print "Welcome to MoarVM!\n\n";

print "Trying to figure out how to build on your system ...";
my %config = Config::BuildEnvironment::detect();
if (!$config{'excuse'}) {
    print "... OK\n    (OS: $config{'os'}, Compiler: $config{'cc'}, Linker: $config{'link'})\n";
}
else {
    print "... FAILED\n";
    die "    Sorry, I'm not sure how to build on this platform:\n    $config{'excuse'}\n";
}

print "Configuring APR ...";
%config = Config::APR::configure(%config);

sub check_excuse {
    if (!$config{'excuse'}) {
        print ".................................... OK\n";
    }
    else {
        print ".................................... FAILED\n";
        die "    $config{'excuse'}\n";
    }
}
check_excuse();

print "Configuring libatomic_ops ...";
%config = Config::LAO::configure(%config);
check_excuse();

print "Generating config.h ...";
Config::Generate::file('build/config.h.in', 'src/gen/config.h', %config);
print "................................ OK\n";

print "Generating Makefile ...";
Config::Generate::file('build/Makefile.in', 'Makefile', %config);
print "................................ OK\n";

print "\nConfiguration successful. Type '$config{'make'}' to build.\n";
