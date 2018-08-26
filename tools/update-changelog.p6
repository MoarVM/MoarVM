#!/usr/bin/env perl6
# This script is meant to automate part of the creation of the ChangeLog.
#
use Git::Log;
class ChangelogClassifier {
    has @.keywords;
    has Str $.title;
    has Str $.directory;
    method has-keyword (Str:D $input) {
        for @!keywords -> $keyword {
            my $created = "[" ~ $keyword ~ "]";
            if $input ~~ /^\s*$created/ {
                return $input.subst(/\s*$created\s*/, ' ').trim;
            }
            $created = "$keyword:";
            if $input ~~ /^\s*$created\s*/ {
                return $input.subst(/^\s*$created\s*/, ' ').trim;
            }
        }
        return False;
    }
}
# @keywords is used to help the categorizer. Keyword is checked first, then
# the directory (which can be either a string or an array of strings) is checked.
# The `title` is the title it should be in the final ChangeLog
my @keywords =
    ChangelogClassifier.new(keywords => 'JIT', directory => 'src/jit', title => 'JIT'),
    ChangelogClassifier.new(keywords => 'Spesh', directory => 'src/spesh', title => 'Spesh'),
    ChangelogClassifier.new(title => 'Strings', directory => 'src/strings'),
    ChangelogClassifier.new(title => 'IO', directory => 'src/io'),
    ChangelogClassifier.new(title => 'Debug Server', directory => 'src/debug', keywords => 'Debugserver'),
    ChangelogClassifier.new(title => '6model', directory => 'src/6model'),
    ChangelogClassifier.new(title => 'Documentation', directory => 'docs'),
    ChangelogClassifier.new(title => 'Tooling/Build', keywords => 'Tools'),
    ChangelogClassifier.new(title => 'Profiler', keywords => 'Profiler', directory => 'src/profiler'),
    ChangelogClassifier.new(title => 'Platform', directory => 'src/platform'),
    ChangelogClassifier.new(title => 'Math', directory => 'src/math'),
    ChangelogClassifier.new(title => 'Core', directory => 'src/core'),
    ChangelogClassifier.new(title => 'GC', directory => 'src/gc')
;
sub git (*@args) {
    my $cmd = run 'git', @args, :out, :err;
    my $out = $cmd.out.slurp;
    my $err = $cmd.err.slurp;
    die "Bad exitcode. Got $cmd.exitcode().\nSTDOUT<<$out>>\nSTDERR<<$err>>"
        if $cmd.exitcode != 0;
    print $err if $err;
    note 'git ', @args.join(' ');
    $out;
}
sub last-tag {
    my $describe = git 'describe';
    $describe .= chomp;
    die unless $describe ~~ s/ '-' <:Hex>+ '-g' <:Hex>+ $ //;
    $describe;
}
#| Just prints git output
sub pgit (|c) {
    print git(c)
}
sub is-expr-jit (Str:D $text) {
    $text ~~ /:i ^ Add \s+ (\S+) \s+ 'exprjit template'/;
    $0 ?? ~$0 !! "";
}
sub format-output ($thing, :$print-modified-files = False) {
    $thing<Subject> ~ (" | $thing.<changes>».<filename>.join(", ")" if $thing<changes> && $print-modified-files);
}
sub MAIN (Bool:D :$print-modified-files = False) {
    my @loggy = git-log "{last-tag()}..master", :get-changes;
    my %categories;
    for @loggy -> $change {
        my $has-pushed = False;
        next if !$change<changes>;
        my $main-dir = $change<changes>.sort(-*.<added>).first<filename>;
        for @keywords -> $keyword {
            my $val = $keyword.has-keyword($change<Subject>);
            if !$has-pushed && $val {
                $change<Subject> = $val;
                %categories{$keyword.title}.push: $change;
                $has-pushed = True;
            }
        }
        for @keywords -> $keyword {
            if !$has-pushed && $keyword.directory && $main-dir.starts-with($keyword.directory) {
                %categories{$keyword.title}.push: $change;
                $has-pushed = True;
            }
        }
        if $has-pushed {

        }
        elsif $main-dir ~~ /^'src/'/ {
            %categories{$main-dir}.push: $change;
        }
        elsif $main-dir ~~ /^'docs/'/ or $main-dir eq 'README.markdown' {
            %categories<docs>.push: $change;
        }
        elsif $main-dir eq 'Configure.pl' | 'build/Makefile.in' | 'build/setup.pm' or $main-dir ~~ /^'tools/'/ {
            %categories<Tooling/Build>.push: $change;
        }
        elsif $main-dir.starts-with('3rdparty') {
            %categories<Libraries>.push: $change;
        }
        elsif $main-dir.starts-with('lib/MAST') {
            %categories<Ops>.push: $change;
        }
        else {
            %categories<Other>.push: $change;
        }

    }
    # Remove the expr jit op additions so we can combine them into one entry
    my @new-expr-jit-ops;
    for %categories<JIT>.list <-> $item {
        my $result = is-expr-jit($item<Subject>);
        if $result {
            @new-expr-jit-ops.push: $result;
            $item = Nil;
        }
    }
    my $has-outputted-expr-jit-ops = False;
    for %categories.keys.sort -> $key {
        my $title;
        my @list;
        if $key eq 'JIT' && !$has-outputted-expr-jit-ops {
            $has-outputted-expr-jit-ops = True;
            my $str = "Add " ~ @new-expr-jit-ops.join(", ") ~ " exprjit ops";
            @list.push: $str;
        }
        @list.append: %categories{$key}.grep({$_}).map({format-output($_, :$print-modified-files)});
        # $print-modified-files is used to print out the files modified at the end of the line
        $title = $key;
        say "\n$title:";
        say '+ ' ~ @list.join("\n+ ");
    }

}
