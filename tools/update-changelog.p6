#!/usr/bin/env perl6
# This script is meant to automate part of the creation of the ChangeLog.
#
use Git::Log;
use JSON::Fast;
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
class ViewOptions {
    has Bool:D $.modified-files is rw = True;
    has Bool:D $.commit is rw         = True;
    has Bool:D $.category is rw       = True;
    has Bool:D $.subject-origin is rw = True;
    has Bool:D $.subject is rw        = True;
    has Bool:D $.author is rw         = False;
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
multi sub format-output ($thing, ViewOptions:D $viewopts) {
    format-output($thing, :author($viewopts.author), :print-modified-files($viewopts.modified-files), :print-commit($viewopts.commit), :print-category($viewopts.category), :print-subject-origin($viewopts.subject-origin));
}
multi sub format-output ($thing, :$author = False, :$print-modified-files = False, :$print-commit = False, :$print-category = False, :$print-subject-origin = False) {
    my @text;
    @text.push: "[$thing<ID>.substr(0, 8)]" if $print-commit;
    @text.push: '{' ~ ($thing<CustomCategory> // $thing<AutoCategory> // "???") ~ '}' if $print-category;
    if $print-subject-origin {
        if $thing<CustomSubject> {
            @text.push: '(Custom)';
        }
        elsif $thing<AutoSubject> {
            @text.push: '(Auto)';
        }
        else {
            @text.push: '(Commit)';
        }
    }
    @text.push: ($thing<CustomSubject> // $thing<AutoSubject> // $thing<Subject>);
    if $thing<changes> && $print-modified-files {
        @text.push: '|';
        @text.push: $thing.<changes>».<filename>.join(", ");
    }
    if $author {
        @text.push: '|';
        @text.push: $thing<AuthorName>;
    }
    @text.join(' ');
}
my $dat-file = "updatechangelog.dat";
sub get-folder-name (Str:D $str) {
    my @split = $str.split('/');
    my $result = ((@split - 1 < 2) ?? @split[0] !! @split[0..1]).join('/');
    $result = $str if $result eq 'src';
    #say "in: $str out: $result";
    return $result;
}
use Test;
is get-folder-name("src/strings/foo.c"), "src/strings";
is get-folder-name("tools/update-changelog.p6"), "tools";
is get-folder-name("src/moar.c"), "src/moar.c";
done-testing;
my $remove-weight = 0.5;
my $addition-weight = 1;
my $has-rlwrap;
sub get-main-dir ($thing) {
    my %folders;
    for $thing<changes>.list -> $change {
        my $folder = get-folder-name($change<filename>);
        %folders{$folder} += $addition-weight * $change<added> + $remove-weight * $change<removed>;
    }
    #say "\%folders.perl: «" ~ %folders.perl ~ "»";
    %folders.sort(-*.value).first.key;
}
# TODO remove %categories
sub do-item ($item, $viewopts, @loggy, %categories, Bool:D :$deep = False) {
    my $not-done = True;
    while ($not-done) {
        say format-output($item, $viewopts);
        my $response = prompt "MODIFY (e)dit/(d)rop/(c)ategory/d(o)ne/(T)itlecase; PRINT (b)ody/d(i)ff/num-(l)eft/(C)omplete/(U)uncomplete; GOTO (n)ext/(G)oto commit ID; (Q)uit and save or (s)ave: ";
        given $response {
            when 'v' {
                say "Toggle view options. (m)odified files, (c)ommit, (C)ategory, subject (o)rigin, (a)uthor, (q)uit this menu and go back";
                my $result = prompt;
                given $result.trim {
                    when 'm' {
                        $viewopts.modified-files = !$viewopts.modified-files;
                    }
                    when 'c' {
                        $viewopts.commit = !$viewopts.commit;
                    }
                    when 'C' {
                        $viewopts.category = !$viewopts.category;
                    }
                    when 'o' {
                        $viewopts.origin = !$viewopts.origin;
                    }
                    when 'a' {
                        $viewopts.author = !$viewopts.author;
                    }
                }
            }
            when 'e' {
                say "Enter a new line q to cancel: ";
                my $result = prompt-it $item<CustomSubject> // $item<AutoSubject> // $item<Subject>;
                if $result.trim.lc ne 'q' && $result.trim ne '' {
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
            when 's' | 'Q' {
                my %hash;
                for @loggy -> $ite {
                    %hash{$ite<ID>} = $ite;
                }
                spurt "updatechangelog.dat", to-json(%hash);
                if $response ~~ /Q/ {
                    exit;
                }

            }
            when 'T' {
                $item<CustomSubject> = ($item<CustomSubject> // $item<AutoSubject> // $item<Subject>).tc;
            }
            when 'C'|'U' {
                say "";
                my $proc = run 'less', :in;
                for @loggy -> $item {
                    next if $item<dropped>;
                    if $response eq 'U' {
                        next if $item<done>;
                    }
                    else {
                        next if !$item<done>;
                    }
                    $proc.in.say(format-output($item, $viewopts));
                }
                $proc.in.close;
                say "";
            }
            when 'G' {
                my $wanted-commit = prompt "Enter the desired commit ID: ";
                my $need = @loggy.first({.<ID>.starts-with($wanted-commit.trim)});
                if $need {
                    if !$deep {
                        while ($need) {
                            $need = do-item $need, $viewopts, @loggy, %categories, :deep;
                        }
                    }
                    # We are already one deep so return it to continue
                    else {
                        return $need;
                    }
                }
                else {
                    say "Can't find commit $wanted-commit";
                }
            }


        }
    }
    Nil;
}
sub prompt-it (Str:D $pretext = '') {
    if !defined $has-rlwrap {
        my $cmd = run 'which', 'rlwrap', :out, :err;
        if $cmd.exitcode == 0 {
            $has-rlwrap = True;
        }
        else {
            $has-rlwrap = False;
        }
    }
    if $has-rlwrap {
        my $cmd = run 'rlwrap', '-P', $pretext, '-o', 'cat', :out, :err;
        if $cmd.exitcode == 0 {
            return $cmd.out.slurp.chomp;
        }
        else {
            note "rlwrap failed with exitcode $cmd.exitcode(), STDERR: $cmd.stderr.slurp()";
            return Nil;
        }
    }
}
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
        my $main-dir = get-main-dir($change);
        #if $change<ID>.starts-with("9861e801") {
        #    say "maindir: «$main-dir»";
        #    exit;
        #}
        for @keywords -> $keyword {
            my $val = $keyword.has-keyword($change<Subject>);
            if !$has-pushed && $val {
                $change<AutoSubject> = $val;
                $change<AutoCategory> = $keyword.title;
                #$change<Subject> = $val;
                %categories{$keyword.title}.push: $change;
                $has-pushed = True;
            }
        }
        for @keywords -> $keyword {
            if !$has-pushed && $keyword.directory && $main-dir eq $keyword.directory {
                %categories{$keyword.title}.push: $change;
                $change<AutoCategory> = $keyword.title;
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
    my $viewopts = ViewOptions.new;
    for @loggy -> $item {
        next if $item<dropped>;
        next if $item<done>;
        do-item($item, $viewopts, @loggy, %categories);
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
