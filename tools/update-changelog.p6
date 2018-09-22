#!/usr/bin/env perl6
# This script is meant to automate part of the creation of the ChangeLog.
#
use Git::Log;
use JSON::Fast;
my $merged-into = "MergedInto";
my $merged-to   = "MergedTo";
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
    has Bool:D $.dropped is rw        = True;
    method format-output ($thing) {
        my @text;
        if self.commit {
            my @all = $thing<ID>;
            @all.append: $thing{$merged-into} if $thing{$merged-into};
            @text.push: '[' ~ @all».substr(0, 8).join(',') ~ ']';
        }
        if self.dropped && $thing<dropped> {
            @text.push: '<dropped>';
        }
        @text.push: '{' ~ ($thing<CustomCategory> // $thing<AutoCategory> // "???") ~ '}' if self.category;
        if self.subject-origin {
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
        if $thing<changes> && self.modified-files {
            @text.push: '|';
            @text.push: $thing.<changes>».<filename>.join(", ");
        }
        if self.author {
            @text.push: '|';
            @text.push: $thing<AuthorName>;
        }
        @text.join(' ');
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
sub format-output ($thing, :$author = False, :$print-modified-files = False, :$print-commit = False, :$print-category = False, :$print-subject-origin = False) {
    my $viewopts = ViewOptions.new(:$author, :modified-files($print-modified-files), :commit($print-commit), :category($print-category), :subject-origin($print-subject-origin));
    $viewopts.format-output($thing);
}
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
sub print-message(Str:D $text, :$status) {
    say "{'  ' if $status }$text";
}
sub get-subject ($item) {
    $item<CustomSubject> // $item<AutoSubject> // $item<Subject>;
}
sub get-category ($item) {
    $item<CustomCategory> // $item<AutoCategory> // "???";
}
sub get-folder-name (Str:D $str) {
    my @split = $str.split('/');
    my $result = ((@split - 1 < 2) ?? @split[0] !! @split[0..1]).join('/');
    $result = $str if $result eq 'src';
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
    %folders.sort(-*.value).first.key;
}
# TODO remove %categories
sub do-item ($item, $viewopts, @loggy, %categories, Str:D $output-filename, Bool:D :$deep = False) {
    my $not-done = True;
    while ($not-done) {
        say $viewopts.format-output($item);
        my $response = prompt "MODIFY (e)dit/(d)rop/(c)ategory/d(o)ne/(T)itlecase/[(m)erge into other commit]/[change (v)iew opts]; " ~
            "PRINT (b)ody/d(i)ff/num-(l)eft/(D)ump/(C)omplete/(U)uncomplete; " ~
            "GOTO (n)ext/(G)oto commit ID; (Q)uit and save or (s)ave: ";
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
                        $viewopts.subject-origin = !$viewopts.subject-origin;
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
                say "There are " ~ @loggy.grep({ !$_<dropped> && !$_<done>}).elems ~ " log items left to deal with";
            }
            when 'c' {
                my @keys = %categories.keys.sort;
                say "\n" ~ @keys.map({ state $foo = 0; "({$foo++}) $_"}).join(' ');
                my $result = prompt "Enter a category number OR type text to set a new category or (b) to go back: \n";
                if $result.trim eq 'b' {
                    say "\nGoing back\n";
                }
                elsif $result ~~ /^\d+$/ && $result < @keys.elems {
                    $item<CustomCategory> = @keys[$result];
                    say "\nSet to $item<CustomCategory>\n";
                }
                # TODO handle custom setting
                elsif $result.trim.chars <= 2 {
                    say "\nRefusing to set a category less than 2 characters. Assuming you made a mistake.\n";
                }
                else {
                    say "\nDidn't know what '$result' is.\n";
                    my $res = prompt "Add custom category called '$result'? y/n: ";
                    if $res.trim.lc eq 'y' {
                        $item<CustomCategory> = $result.trim;
                    }
                }


            }
            when 'D' {
                print-message "Dumping this commit:\n", :status;
                say $item.perl;
                say "";
            }
            # TODO: goto the commit we are merging into so we can edit the subject
            when 'm' {
                my $merge-into = prompt "Merge into which commit?: ";
                my $need = @loggy.first({.<ID>.starts-with($merge-into.trim)});
                if !$need {
                    say "\nERROR: Can't find commit $need\n";
                }
                elsif $item<ID>.lc.starts-with($merge-into.lc) {
                    say "\nCannot merge a commit into itself!\n";
                }
                elsif $item{$merged-to} && $item{$merged-to}.first(*.starts-with($merge-into.trim)) {
                    say "\nERROR: This has already been merged into that one\n";
                }
                else {
                    $item{$merged-to}.push: $need<ID>;
                    $item<done> = True;
                    $need{$merged-into}.push: $item<ID>;
                    $need<done> = False;
                    $need<CustomSubject> = get-subject($need) ~ ', ' ~ get-subject($item);
                }
            }

            when 'd' {
                if $item<dropped> {
                    print-message "UNdropped commit", :status;
                    $item<dropped>:delete;
                }
                else {
                    $item<dropped> = "User";
                    print-message "DROPped commit", :status;
                }
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
                spurt $output-filename, to-json(%hash);
                if $response ~~ /Q/ {
                    exit;
                }

            }
            when 'T' {
                $item<CustomSubject> = ($item<CustomSubject> // $item<AutoSubject> // $item<Subject>).tc;
            }
            when 'C'|'U'|'Cs'|'Us'|'UsS'|'CsS' {
                say "";
                my $proc = $response.contains('S') ?? run 'tee', 'out.txt', :in !! run 'less', :in;
                my %categories;
                for @loggy.reverse -> $item {
                    next if $item<dropped>;
                    next if $item{$merged-to};
                    if $response.starts-with('U') {
                        next if $item<done>;
                    }
                    else {
                        next if !$item<done>;
                    }
                    if $response.contains('s') {
                        %categories{ get-category($item) }.push: $item;
                    }
                    else {
                        $proc.in.say: $viewopts.format-output($item);
                    }
                }
                if $response.contains('s') {
                    for %categories.keys.sort -> $key {
                        for %categories{$key}.list -> $item {
                            $proc.in.say($viewopts.format-output($item));
                        }
                    }
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
                            $need = do-item $need, $viewopts, @loggy, %categories, $output-filename, :deep;
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
sub next-tag (Str:D $tag) {
    my ($year, $month) = $tag.split('.');
    if $month == 12 {
        $year++;
        $month = 1;
    }
    else {
        $month++;
    }
    sprintf "%i.%.2i", $year, $month;
}
class OutputFile {
    has IO::Path $.filename;
    method parse {
    }
}
sub MAIN (Bool:D :$print-modified-files = False, Bool:D :$print-commit = False) {
    my $last-tag = last-tag();
    my $next-tag = next-tag($last-tag);
    my $output-filename = $next-tag ~ "-changelog.json";
    my @loggy = git-log '--topo-order', "{last-tag()}..master", :get-changes;
    my %categories;
    my $dat = OutputFile.new(:filename($output-filename.IO));
    if $output-filename.IO.f {
        my $answer = 'y';
        while $answer ne 'y' && $answer ne 'n' {
            $answer = prompt "Load saved changelog info file? y/n: ";
        }
        if $answer eq 'y' {
            my %hash = from-json $output-filename.IO.slurp;
            for @loggy <-> $logone {
                if %hash{$logone<ID>} {
                    $logone = %hash{$logone<ID>};
                }
            }
        }
    }
    else {
        say "file isn't here";
    }
    for @loggy -> $change {
        my $has-pushed = False;
        next if !$change<changes>;
        my $main-dir = get-main-dir($change);
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
    my $viewopts = ViewOptions.new;
    for @loggy.reverse -> $item {
        next if $item<dropped>;
        next if $item<done>;
        do-item($item, $viewopts, @loggy, %categories, $output-filename);
    }
    my $prompt-result;
    while !$prompt-result || $prompt-result ne 'y'|'n' {
        $prompt-result = prompt "Should we save y/n?: ";
    }
    if $prompt-result eq 'y' {
        my %hash;
        for @loggy -> $ite {
            %hash{$ite<ID>} = $ite;
        }
        spurt $output-filename, to-json(%hash);
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
