#!/usr/bin/env perl
use strict;
use warnings;
use FindBin;
use Getopt::Long;
use File::Temp 'tempdir';
use File::Spec;
use File::Copy;


my $interpreter = 'perl6';
my $test;
my $command;
my $verbose;
my $objdump;
my @libs;


GetOptions(
    'test=s' => \$test,
    'interpreter=s' => \$interpreter,
    'lib=s' => \@libs,
    'command=s' => \$command,
    'verbose' => \$verbose,
    'objdump=s' => \$objdump,
);

my @command;
if (defined $command) {
    # TODO: this wants some better evaluation strategy rather than
    # removing " blindly
    @command = map s/"//rg, split / /, $command;
} else {
    unless (defined($test)) {
        $test = pop @ARGV;
    }

    unless (defined $command || defined($test) && -f $test) {
        print STDERR "Pass a test file argument";
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

    @command = ('prove', '-e', $interpreter, (@libs ? '-I' : ''), @libs, $test);
}


$objdump = ($^O =~ m/darwin/ ? 'gobjdump' : 'objdump') unless defined $objdump;

my $bytecode_dir = tempdir();
$ENV{'MVM_JIT_BYTECODE_DIR'} = $bytecode_dir;

# clone stdout, stderr
open my $stdout, '>&', STDOUT;
open my $stderr, '>&', STDERR;

# silence prove
unless ($verbose) {
    close STDOUT;
    close STDERR;
    open STDOUT, '>', '/dev/null';
    open STDERR, '>', '/dev/null';
}
# do initial run
my $code = system @command;
if ($code == 0) {
    print $stdout "Test is OK\n";
    exit;
}

open my $frame_map_fh, '<', "$bytecode_dir/jit-map.txt";
my %frame_map = map { my @a = split /\s/; $a[0] => "$a[1] $a[2]" } <$frame_map_fh>;
close $frame_map_fh;

my $num_frames = scalar keys %frame_map;
print $stdout "$num_frames compiled in test\n";

# don't write more bytecode files
$ENV{'MVM_JIT_BYTECODE_DIR'} = '';

my $min = 1;
my $max = $num_frames;
my $mid;
while (($max - $min) > 1) {
    $mid = int(($max + $min) / 2);
    $ENV{'MVM_JIT_EXPR_LAST_FRAME'} = $mid;
    print $stdout "Testing up to $mid frames....";
    $code = system @command;
    if ($code == 0) {
        # ok
        print $stdout "OK\n";
        $min = $mid;
    } else {
        print $stdout "Broken\n";
        $max = $mid;
    }
}
my $frame_nr = $max;
print $stdout "Broken at frame $frame_nr\n";
my $frame_file = sprintf("%s/moar-jit-%04d.bin", $bytecode_dir, $frame_nr);

print $stdout $frame_file, $frame_map{$frame_file}, "\n";


# exponential probing of breaking basic block
$ENV{'MVM_JIT_EXPR_LAST_FRAME'} = $max;
$code = 0;
$min = 1;
do {
    $ENV{'MVM_JIT_EXPR_LAST_BB'} = $min;
    print $stdout "Probing $min basic blocks\n";
    $code = system @command;
    $min *= 2 if $code == 0;
} while ($code == 0);

$max = $min;
$min = $min / 2;
while (($max - $min) > 1) {
    $mid = int(($max + $min) / 2);
    $ENV{'MVM_JIT_EXPR_LAST_BB'} = $mid;
    print $stdout "Testing $mid basic blocks...";
    $code = system @command;
    if ($code == 0) {
        print $stdout "OK\n";
        $min = $mid;
    } else {
        print $stdout "Broken\n";
        $max = $mid;
    }
}
my $basic_block_nr = $max;
print $stdout "Broken basic block nr $basic_block_nr of frame $frame_nr\n";

# now, for a final trick
print $stdout "Acquiring code diff...\n";
$ENV{'MVM_JIT_BYTECODE_DIR'} = $bytecode_dir;
$ENV{'MVM_JIT_EXPR_LAST_BB'} = $basic_block_nr;
$ENV{'MVM_JIT_LOG'}          = 'broken-log.txt';
# get the broken file
system @command;
copy($frame_file, 'broken-frame.bin');
$ENV{'MVM_JIT_EXPR_LAST_BB'} = $basic_block_nr - 1;
$ENV{'MVM_JIT_LOG'}          = 'working-log.txt';
# and the working file
system @command;
copy($frame_file, 'working-frame.bin');

# disassemble and make comparable
{
    no warnings 'qw';

    close STDOUT;
    open STDOUT, '>', "broken-frame.asm";
    system($objdump, qw(-b binary -D -m i386 -M x86-64,intel broken-frame.bin)) == 0
        or die "Error running $objdump";
    close STDOUT;

    open STDOUT, '>', "working-frame.asm";
    system($objdump qw(-b binary -D -m i386 -M x86-64,intel working-frame.bin)) == 0
        or die "Error running $objdump";
    close STDOUT;

    my $script = "$FindBin::Bin/jit-comparify-asm.pl";
    open STDOUT, '>', '/dev/null';

    system $^X, '-i', $script, "broken-frame.asm";
    system $^X, '-i', $script, "working-frame.asm";
}
