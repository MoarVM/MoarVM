#!/usr/bin/env perl6
# This script is meant to automate part of the creation of the ChangeLog.
# Some of it could be cleaned up, such as the get-log() sub, but it performs its
# function. For editing
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
sub shift-blank (@array) {
    my $temp = @array.shift;
    die "temp: '$temp'" unless !$temp.trim;
}
my @changes;
sub get-log {
    my $last-tag = last-tag();
    my $log = git 'log', '--date-order', '--no-merges', "$last-tag..master", "--stat";
    my Int:D $number = git('log', '--date-order', '--no-merges', "$last-tag..master", "--oneline").lines.elems;
    my @split = split(/^^'commit '/, $log).grep({$_ !~~ /^\s*$/});
    die if @split != $number;
    for @split -> $chunk {
        my @lines = $chunk.lines;
        my $commit = @lines.shift.trim;
        my $author = @lines.shift.trim;
        die "not author $author" unless $author ~~ /Author/;
        my $date = @lines.shift.trim;
        my $temp = @lines.shift.trim;
        die $temp if $temp !~~ /^\s*$/;
        my $topic = @lines.shift.trim.tc;
        $temp = @lines.shift.trim;
        die $temp if $temp;
        my @description;
        while @lines[0].starts-with('    ') {
            my $desc = @lines.shift;
            $desc .= trim;
            @description.push: $desc;
        }
        my $description = @description.join("\n");
        @lines.shift if !@lines[0].trim;
        my @files;
        while @lines[0] !~~ /^\s*$/ {
            @files.push: @lines.shift.trim;
        }
        my $summary = @files.pop;
        my @file-done;
        for @files -> $file-raw {
            die unless $file-raw ~~ /$<file>=(.*\S)\s+ '|' \s+ $<num>=(\d+)/;
            @file-done.push: ~$<file> => $<num>.Int;
        }
        #say $summary;
        #say @file-done;
        my %all = author => $author, date => $date, topic => $topic, description => $description, files => @file-done;
        push @changes, %all;
        #say @description.join("\n");
    }
}
sub pgit (|c) {
    print git(c)
}
pgit 'describe';
#say last-tag;
get-log();
my @out2;
for @changes -> $change {
    #say $change<topic>;
    my @out;
    for $change<files>.sort(-*.value) -> $file {
        my @parts = $file.key.split('/').grep({$_ ne '...' });
        if @parts[0] eq '3rdparty' {
            @out.push: '3rdparty', @parts[1];
        }
        elsif @parts > 1 {
            @out.push: @parts[0..1].join('/');
        }
        else {
            @out.push: @parts[0];
        }
    }
    push @out2, $change<topic> => @out.unique;
}
my %categories;
class datay {
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
    datay.new(keywords => 'JIT', directory => 'src/jit', title => 'JIT'),
    datay.new(keywords => 'Spesh', directory => 'src/spesh', title => 'Spesh'),
    datay.new(title => 'Strings', directory => 'src/strings'),
    datay.new(title => 'IO', directory => 'src/io'),
    datay.new(title => 'Debug Server', directory => 'src/debug', keywords => 'Debugserver'),
    datay.new(title => '6model', directory => 'src/6model'),
    datay.new(title => 'Documentation', directory => 'docs'),
    datay.new(title => 'Tooling/Build', keywords => 'Tools'),
    datay.new(title => 'Profiler', keywords => 'Profiler', directory => 'src/profiler'),
        datay.new(title => 'Platform', directory => 'src/platform');
for @out2 -> $change {
    my $has-pushed = False;
    for @keywords -> $keyword {
        my $val = $keyword.has-keyword($change.key);
        if !$has-pushed && $val {
            my $copy = Pair.new($val, $change.value);
            %categories{$keyword.title}.push: $copy;
            $has-pushed = True;
        }
    }
    for @keywords -> $keyword {
        if !$has-pushed && $keyword.directory && $change.value[0].starts-with($keyword.directory) {
            %categories{$keyword.title}.push: $change;
            $has-pushed = True;
        }
    }

    #if $change.key.contains: '[JIT]' {
    #    my $title = $change.key.subst(/:i \s*'[JIT]'\s*/, ' ').trim;
    #    my $copy = Pair.new($title, $change.value);
    #    %categories<src/jit>.push: $copy;
    #}
    if $has-pushed {

    }
    elsif $change.value[0] ~~ /^'src/'/ {
        %categories{$change.value[0]}.push: $change;
    }
    elsif $change.value[0] ~~ /^'docs/'/ or $change.value[0] eq 'README.markdown' {
        %categories<docs>.push: $change;
    }
    elsif $change.value[0] eq 'Configure.pl' | 'build/Makefile.in' | 'build/setup.pm' or $change.value[0] ~~ /^'tools/'/ {
        %categories<Tooling/Build>.push: $change;
    }
    elsif $change.value[0] eq '3rdparty' {
        %categories<Libraries>.push: $change;
    }
    elsif $change.value[0] eq 'lib/MAST' {
        %categories<Ops>.push: $change;
    }
    else {
        %categories<Other>.push: $change;
        #say $change;
    }

}
my %names =
    'src/spesh' => 'Spesh',
    'src/strings' => 'Strings',
    'src/profiler' => 'Profiler',
    'src/debug' => 'Debugger',
    'src/jit'   => 'JIT';
for %categories.keys.sort -> $key {
    my $title;
    # $extra is used to print out the files modified at the end of the line
    my $extra = False;
    #if %names{$key} {
    #    $title = %names{$key};
    #}
    #else {
        $title = $key;
    #}
    say "\n$title:";
    say '+ ' ~ %categories{$key}.map({"$_.key()" ~ (" | $_.value.join(", ")" if .value && $extra)}).join("\n+ ");
}
