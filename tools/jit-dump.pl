#!/usr/bin/env perl
use strict;
use warnings;
use FindBin;
use File::Spec;
use lib File::Spec->catdir($FindBin::Bin, 'lib');

use timeout qw(run_timeout);


use File::Temp qw(tempdir);
use File::Copy qw(copy);
use Getopt::Long;

my %OPTIONS = (
    dir =>   '.',
    arch => 'x64',
    timeout => 0,
);

GetOptions(
    \%OPTIONS,
    qw(frame=i@ block=i@ objdump=s directory=s arch=s timeout=i)
) or die "Could not parse options";

delete @ENV{qw(
    MVM_SPESH_DISABLE
    MVM_JIT_DISABLE
    MVM_JIT_EXPR_DISABLE
)};
$ENV{$_} = 1 for qw(MVM_SPESH_BLOCKING MVM_JIT_DUMP_BYTECODE MVM_JIT_DEBUG);

die "--frame and --block required" unless $OPTIONS{frame} and $OPTIONS{block};
my @command = @ARGV;
die "Command required" unless @command;
my @binary;

my $timeout = delete $OPTIONS{timeout};
push @{$OPTIONS{block}}, $OPTIONS{block}[0] - 1 if @{$OPTIONS{block}} == 1;
my $dump_directory = delete $OPTIONS{directory} || '.';

for my $frame (@{$OPTIONS{frame}}) {
    $ENV{MVM_JIT_EXPR_LAST_FRAME}    = $frame;
    for my $block (@{$OPTIONS{block}}) {
        $ENV{MVM_JIT_EXPR_LAST_BB} = $block;
        $ENV{MVM_SPESH_LOG} = sprintf('spesh-log-%04d-%04d.txt', $frame, $block);
        my ($result, $pid) = run_timeout(\@command, $timeout);
        my $log_directory = File::Spec->catdir(File::Spec->tmpdir, "moar-jit.$pid");
        my $filename = File::Spec->catfile($log_directory, sprintf('moar-jit-%04d.bin', $frame));
        printf("Want to copy: %s\n", $filename);
        my $bin_out  = File::Spec->catfile($dump_directory, sprintf('moar-jit-%04d-%04d.bin', $frame, $block));
        copy ($filename, $bin_out) or die "Could not copy binary: $!";
        push @binary, $bin_out;
    }
}

my $objdump = $OPTIONS{objdump} || do {
    no warnings 'exec';
    my $program;
    for (qw(objdump gobjdump)) {
        $program = $_ and last if system($_, '-v') == 0;
    }
    die "Cannot find objdump program" unless $program;
    $program;
};


my %OBJDUMP_FLAGS = do {
    no warnings 'qw';
    (
        x64 => [qw(-b binary -m i386 -M x86-64,intel -D)],
    );
};

sub disassemble_and_comparify {
    local $" = " ";
    my ($binary) = @_;
    my @objdump_command = ($objdump, @{$OBJDUMP_FLAGS{$OPTIONS{arch}}}, $binary);
    my @comparify_command = ($^X, File::Spec->catfile($FindBin::Bin, 'jit-comparify-asm.pl'));
    my $out_file = $binary =~ s/\.bin$/.asm/ir;
    my ($in_pipe, $out_pipe);
    pipe $in_pipe, $out_pipe;
    my $objdump_pid = fork();
    if ($objdump_pid == 0) {
        print STDERR "Starting `@objdump_command`\n";
        close( STDOUT ) or die $!;
        open( STDOUT, '>&', $out_pipe) or die $!;
        exec @objdump_command or die "Could not exec objdump";
    }
    my $comparify_pid = fork();
    if ($comparify_pid == 0) {
        print STDERR "Starting `@comparify_command`\n";
        close( STDIN ) or die $!;
        open( STDIN, '<&', $in_pipe ) or die $!;
        close( STDOUT ) or die $!;
        open( STDOUT, '>', $out_file ) or die $!;
        exec @comparify_command or die "Could not exec comparify";
    }
    return ($objdump_pid, $comparify_pid);
}

if ($objdump && $OBJDUMP_FLAGS{$OPTIONS{arch}}) {
    my @pid;
    for my $binary (@binary) {
        push @pid, disassemble_and_comparify($binary);
    }
    my $child_id;
    do {
        $child_id = waitpid(-1, 0);
    } while ($child_id > 0);
} else {
    printf STDERR "objdump not found, skipping\n";
}
