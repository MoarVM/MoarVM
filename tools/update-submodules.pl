#!/usr/bin/env perl
# Copyright (C) 2009-2019 The Perl Foundation

use 5.10.1;
use strict;
use warnings;
use Getopt::Long;
use Cwd;
use File::Spec;

my $repo = shift @ARGV;
chdir $repo;

exit 0 if !-d '.git';

my $git_cache_dir;
Getopt::Long::Configure("pass_through");
Getopt::Long::GetOptions('git-cache-dir=s' => \$git_cache_dir);

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

if ($git_cache_dir) {
    my $out = exec_with_output('git', 'submodule', 'status');
    if ($? >> 8 != 0) {
        print "\n===SORRY=== ERROR: Submodule status failed for an unknown reason.\n";
        print "The error message was: $out\n";
        exit 1;
    }
    for my $smodline (split(/^/m, $out)) {
        chomp $smodline;
        if ($smodline !~ /^.[0-9a-f]+ ([^ ]+)(?:$| )/) {
            print "\n===SORRY=== ERROR: "
              . "Submodule status output looks unexpected: '$smodline'";
            exit 1;
        }
        my $smodpath = $1;
        my $smodname = (File::Spec->splitdir($smodpath))[-1];
        my $modrefdir = File::Spec->catdir($git_cache_dir, $smodname);
        my $url = exec_with_output('git', 'config', "submodule.$smodpath.url");
        chomp $url;
        if (!$url) {
            print "Couldn't retrieve submodule URL for submodule $smodname\n";
            exit 1;
        }
        if (!-e $modrefdir) {
            exec_and_check('git', 'clone', '--quiet', '--bare', $url, $modrefdir, "Git clone of $url failed.");
        }
        else {
            my $back = Cwd::cwd();
            chdir $modrefdir;
            exec_and_check('git', 'fetch', '--quiet', '--all', "Git fetch in $modrefdir failed.");
            chdir $back;
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
    open(my $handle, '-|', @command);
    my $out;
    while(<$handle>) {
        $out .= $_;
    }
    close $handle;
    return $out;
}

sub exec_and_check {
    my $msg = pop;
    my @command = @_;
    my $out = exec_with_output(@command);
    if ($? >> 8 != 0) {
        print "\n===SORRY=== ERROR: $msg\n";
        print "The program's output was: $out\n";
        exit 1;
    }
}
