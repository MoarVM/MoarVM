#!/usr/bin/env perl
# Copyright (C) 2009-2019 The Perl Foundation

use 5.8.4;
use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;
use Cwd;
use File::Spec;

my ($back, $git_cache_dir);
GetOptions('git-cache-dir=s' => \$git_cache_dir)
    or pod2usage(1);

if (-d ".git" || (exists $ENV{GIT_DIR} && -d $ENV{GIT_DIR})) {
    # We assume a conventional git directory
}
elsif (-f '.git') {
    # Also handle linked worktrees created by git-worktree:
    # the hash of the initial commit in MoarVM
    my $commit = '51481efadbf5bbebfc20767c07056bd2dfc2fabc';
    my $out = exec_with_output(qw(git rev-parse --verify --quiet), "$commit^{commit}");
    chomp $out;
    exit 0
        unless $out eq $commit;
}
else {
    exit 0;
}

print 'Updating submodules .................................... ';

# This git --version parsing logic adapted from the perl5 commit
# 962ff9139336a4a2 which I wrote almost 10 years ago, and seems to have been
# reliable and trouble free ever since:

my $version_string = exec_with_output('git', '--version') // "";
unless ($version_string =~ /\Agit version (\d+\.\d+\.\d+)(.*)/) {
    print "\n===SORRY=== ERROR: `git --version` failed to return a parseable version string\n";
    print "The program's output was: $version_string\n";
}

# CentOS 7 (and presumably similar vintage RedHat) ships 1.8.3
# 1.8.4 and later have the --depth flag.
# However, --quiet wasn't quiet with --depth, until commit 62af4bdd423f5f39,
# which is in 2.32.0
my $git_new_enough = eval "v$1 ge v2.32.0";

exec_and_check('git', 'submodule', 'sync', '--quiet', 'Submodule sync failed for an unknown reason.');
exec_and_check('git', 'submodule', '--quiet', 'init', 'Submodule init failed for an unknown reason.');

if (defined $git_cache_dir) {
    my $out = exec_with_output('git', 'submodule', 'status');
    if ($? >> 8 != 0) {
        print STDERR "\n===SORRY=== ERROR: Submodule status failed for an unknown reason.\n";
        print STDERR "The output message was: $out\n";
        exit 1;
    }
    for my $smodline (split(/^/m, $out)) {
        chomp $smodline;
        if ($smodline !~ /^.[0-9a-f]+ ([^ ]+)(?:$| )/) {
            print STDERR "\n===SORRY=== ERROR: "
              . "Submodule status output looks unexpected: '$smodline'";
            exit 1;
        }
        my $smodpath = $1;
        my $smodname = (File::Spec->splitdir($smodpath))[-1];
        my $modrefdir = File::Spec->catdir($git_cache_dir, $smodname);
        my $url = exec_with_output('git', 'config', "submodule.$smodpath.url");
        chomp $url;
        if (!$url) {
            die "Couldn't retrieve submodule URL for submodule $smodname";
        }
        if (!-e $modrefdir) {
            exec_and_check('git', 'clone', '--quiet', '--bare', $url, $modrefdir, "Git clone of $url failed.");
        }
        else {
            $back = Cwd::cwd()
                unless defined $back;
            chdir $modrefdir
                or die "chdir $modrefdir failed: $!";
            exec_and_check('git', 'fetch', '--quiet', '--all', "Git fetch in $modrefdir failed.");
            chdir $back
                or die "chdir $back failed: $!";
        }
        exec_and_check('git', 'submodule', '--quiet', 'update', '--reference', $modrefdir, $smodpath, 'Git submodule update failed.');
    }
}
elsif ($git_new_enough) {
    # This saves about 38M.
    # If you need the history, run `git submodule foreach git fetch --unshallow`
    exec_and_check('git', 'submodule', '--quiet', 'update', '--depth', '1',
                   'Git submodule update with --depth 1 failed.');
}
else {
    exec_and_check('git', 'submodule', '--quiet', 'update', 'Git submodule update failed.');
}

print "OK\n";


# Helper subs.

sub exec_with_output {
    my @command = @_;
    open(my $handle, '-|', @command)
        or die "piped open @command failed: $!";
    local $/;
    my $out = <$handle>;
    close $handle
        or die "piped close @command failed: $!";
    return $out;
}

sub exec_and_check {
    my $msg = pop;
    my @command = @_;
    my $out = exec_with_output(@command);
    if ($? >> 8 != 0) {
        print STDERR "\n===SORRY=== ERROR: $msg\n";
        print STDERR "The program's output was: $out\n";
        exit 1;
    }
}
