#!perl
use strict;
use warnings;
use lib 'build';
use Config::BuildEnvironment;
use Config::APR;
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
if (!$config{'excuse'}) {
    print ".................................... OK\n";
}
else {
    print ".................................... FAILED\n";
    die "    $config{'excuse'}\n";
}

print "Generating Makefile ...";
Config::Generate::file('build/Makefile.in', 'Makefile', %config);
print "................................ OK\n";

print "\nConfiguration successful. Type '$config{'make'}' to build.\n";
