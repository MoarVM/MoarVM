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
    my @result;
    if $text ~~ /:i ^ Add \s+ (\S+) \s+ 'exprjit template'/ {
        @result.push: ~$0;
    }
    elsif $text ~~ /:i ^ 'Add' \s+ (\S+) \s+ and \s+ (\S+) \s+ exprjit \s+ templates $ / {
        @result.append: ~$0, ~$1;
    }
    @result;
}
sub format-output ($thing, :$print-modified-files = False, :$print-commit = False, :$print-category = False) {
    ("[$thing<ID>.substr(0, 8)] " if $print-commit) ~
    ('{' ~ ($thing<CustomCategory> // $thing<AutoCategory> // "???") ~ '} ' if $print-category) ~
    ($thing<CustomSubject> // $thing<AutoSubject> // $thing<Subject>) ~
    (" | $thing.<changes>».<filename>.join(", ")" if $thing<changes> && $print-modified-files);
}
my $dat-file = "updatechangelog.dat";
sub MAIN (Bool:D :$print-modified-files = False, Bool:D :$print-commit = False) {
    my @loggy = git-log "{last-tag()}..master", :get-changes;
    my %categories;
    if $dat-file.IO.f {
        my $answer = 'y';
        while $answer ne 'y' && $answer ne 'n' {
            $answer = prompt "Load saved changelog info file? y/n: ";
        }
        if $answer eq 'y' {
            my %hash = from-json $dat-file.IO.slurp;
            for @loggy <-> $logone {
                if %hash{$logone<ID>} {
                    $logone = %hash{$logone<ID>};
                }
            }
        }
    }
    for @loggy -> $change {
        my $has-pushed = False;
        next if !$change<changes>;
        my $main-dir = $change<changes>.sort(-*.<added>).first<filename>;
        for @keywords -> $keyword {
            my $val = $keyword.has-keyword($change<Subject>);
            if !$has-pushed && $val {
                $change<AutoSubject> = $val;
                #$change<Subject> = $val;
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
            $change<AutoCategory> = $main-dir;
            %categories{$main-dir}.push: $change;
        }
        elsif $main-dir ~~ /^'docs/'/ or $main-dir eq 'README.markdown' {
            $change<AutoCategory> = "Docs";
            %categories<Docs>.push: $change;
        }
        elsif $main-dir eq 'Configure.pl' | 'build/Makefile.in' | 'build/setup.pm' or $main-dir ~~ /^'tools/'/ {
            $change<AutoCategory> = "Tooling/Build";
            %categories<Tooling/Build>.push: $change;
        }
        elsif $main-dir.starts-with('3rdparty') {
            $change<AutoCategory> = "Libraries";
            %categories<Libraries>.push: $change;
        }
        elsif $main-dir.starts-with('lib/MAST') {
            $change<AutoCategory> = "Ops";
            %categories<Ops>.push: $change;
        }
        else {
            $change<AutoCategory> = "Other";
            %categories<Other>.push: $change;
        }

    }
    # Remove the expr jit op additions so we can combine them into one entry
    my @new-expr-jit-ops;
    for %categories<JIT>.list <-> $item {
        my $result = is-expr-jit($item<Subject>);
        if $result {
            @new-expr-jit-ops.append: $result.list;
            $item<dropped> = 'auto';
        }
    }
    for @loggy -> $item {
        next if $item<dropped>;
        next if $item<done>;
        my $not-done = True;
        while ($not-done) {
            say format-output($item, :print-modified-files, :print-commit, :print-category);
            my $response = prompt "(e)dit/(d)rop/(c)ategory/(n)ext/d(o)ne or print (b)ody/d(i)ff/num-(l)eft or (Q)uit and save or (s)ave: ";
            given $response {
                when 'e' {
                    my $result = prompt "Enter a new line q to cancel: ";
                    if $result.trim.lc ne 'q' {
                        $item<CustomSubject> = $result;
                    }
                }
                when 'l' {
                    say "There are " ~ @loggy.grep({ !$_<dropped> && !$_<done> }).elems ~ " log items left to deal with";
                }
                when 'c' {
                    my @keys = %categories.keys.sort;
                    say "\n" ~ @keys.map({ state $foo = 0; "({$foo++}) $_"}).join(' ');
                    my $result = prompt "Enter a category number OR type text to set a new category or (b) to go back: \n";
                    if $result.trim eq 'b' {

                    }
                    elsif $result ~~ /^\d+$/ && $result < @keys.elems {
                        $item<CustomCategory> = @keys[$result];
                    }
                    # TODO handle custom setting
                    elsif $result.trim.chars <= 2 {
                        say "Refusing to set a category less than 2 characters. Assuming you made a mistake.";
                    }
                    else {
                        say "Didn't know what '$result' is."
                    }


                }
                when 'd' {
                    $item<dropped> = "User";
                    $not-done = False;
                }
                when 'i' {
                    run 'git', 'log', '-p', $item<ID>;
                }
                when 'b' {
                    say "\n" ~ $item<Body>;
                }
                when 'n' {
                    $not-done = False;
                }
                when 'o' {
                    $item<done> = True;
                    $not-done = False;
                }
                # save
                when / <[sS]> | Q / {
                    use JSON::Fast;
                    my %hash;
                    for @loggy -> $ite {
                        %hash{$ite<ID>} = $ite;
                    }
                    spurt "updatechangelog.dat", to-json(%hash);
                    if $response ~~ /Q/ {
                        exit;
                    }

                }

            }
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
        @list.append: %categories{$key}.grep({!$_<dropped>}).map({format-output($_, :$print-modified-files, :$print-commit)});
        # $print-modified-files is used to print out the files modified at the end of the line
        $title = $key;
        say "\n$title:";
        say '+ ' ~ @list.join("\n+ ");
    }

}
