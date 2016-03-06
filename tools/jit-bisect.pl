#!/usr/bin/env perl
use Getopt::Long;
use File::Temp 'tempdir';
use File::Spec;
use strict;

my $interpreter = 'perl6';
my $test;
my @libs;
local $\ = "\n";


GetOptions(
    'test=s' => \$test,
    'interpreter=s' => \$interpreter,
    'lib=s' => \@libs
    );

unless (defined($test)) {
    $test = pop @ARGV;
}

if (!defined($test) || ! -f $test) {
    print "Pass a test file argument";
    exit 1;
}

# add lib for standard module layout
unless (@libs) {
    my ($v, $d, $n) = File::Spec->splitpath($test);
    my @dirs = File::Spec->splitdir($d);
    if ($dirs[-2] eq 't') {
        $dirs[-2] = 'lib';
        my $lib = File::Spec->catpath($v, File::Spec->catdir(@dirs));
        push @libs, $lib if -d $lib;
    }
}

my $bytecode_dir = tempdir();
$ENV{'MVM_JIT_BYTECODE_DIR'} = $bytecode_dir;
my @command = ('prove', '-Q', '-e', $interpreter, (@libs ? '-I' : ''), @libs, $test);

# do initial run
my $code = system @command;
if ($code == 0) {
    print "Test is OK";
    exit;
}

open my $frame_map_fh, '<', "$bytecode_dir/jit-map.txt";
my %frame_map = map {  split /\t/; } <$frame_map_fh>;
close $frame_map_fh;

my $num_frames = scalar keys %frame_map;

$ENV{'MVM_JIT_BYTECODE_DIR'} = '';

my $min = 1;
my $max = $num_frames;
my $mid;
while (($max - $min) > 1) {
    $mid = int(($max + $min) / 2);
    $ENV{'MVM_JIT_EXPR_LAST_FRAME'} = $mid;
    print "Testing up to $mid frames";
    my $code = system @command;
    if ($code == 0) {
        # ok
        $min = $mid;
    } else {
        $max = $mid;
    }
}
print "Breaks at frame $mid";
