use v6;

multi sub MAIN() {
    say q:to/USAGE/;
        This tool can be used as followed:

            spesh_diff.p6 output_from_mvm.txt

            spesh_diff.p6 --matcher="'foobar'" output.txt

            spesh_diff.p6 --matcher="/^ to $/" output.txt

            spesh_diff.p6 --matcher="diff => / '+' .*? 'nyi' /" output.txt


        This tool takes the output MoarVM generates to a file when you call it
        with MVM_SPESH_LOG environment variable set to a filename and splits it
        into two folders, optionally selects a subset of the participating cuids
        and then diffs everything for you using git diff.

        It will output colors always, so if you want to use less to paginate, you
        will need to supply -r to less.

        You can supply any valid Perl 6 code for the matcher flag. If you don't
        supply a Pair, a Pair from name to your matcher will be generated for
        you. You can filter based on these properties:

            before      all text in the "before" section
            after       all text in the "after" section
            name        the name of the method/sub/...
            cuid        the cuid
            diff        the output of the diff command
                        (watch out, these contain ansi color codes)
        USAGE
}

class Spesh is rw {
    has @.beforelines;
    has @.afterlines;
    has $.before;
    has $.after;
    has Str $.name;
    has Str $.cuid;
    has Str $.diff;
}

enum Target <Before Logged Facts Specialized>;

sub supersmartmatch($thing) {
    given $thing {
        when Pair {
            say "making a supersmartmatcher for $thing.key() -> $thing.value().perl()";
            return -> $to_match { $to_match."$thing.key()"() ~~ supersmartmatch($thing.value) }
        }
        when Str {
            return -> $to_match { $to_match ~~ / $($thing) / }
        }
        when Junction {
            return -> $to_match { $to_match ~~ supersmartmatch($thing) }
        }
        default {
            return $thing
        }
    }
}

multi sub MAIN($filename?, :$matcher?) {
    my %speshes;
    my Target $target;

    my $linecount;
    my Int $lines_total;

    if $filename {
        $lines_total = qqx/ wc -l '$filename' /.words[0].Int;
        $*ARGFILES = open($filename, :r);
    } else {
        $*ARGFILES = $*IN;
    }

    my $ssm = do if $matcher {
        my Mu $matcher_evald = EVAL $matcher;
        if $matcher_evald.WHAT ~~ Str | Regex | Junction {
            supersmartmatch (name => $matcher_evald);
        } else {
            supersmartmatch $matcher_evald;
        }
    } else {
        True
    }

    sub notegraph($kind) {
        state $printed = 0;
        state $few = 0;
        my $did_print = False;
        if $kind eq "." {
            if ++$few %% 50 {
                $*ERR.print(".");
                $did_print = True;
            }
        } else {
            $*ERR.print($kind);
            $did_print = True;
        }
        if $did_print and ++$printed %% 80 {
            if $lines_total {
                my $part = $linecount / $lines_total;
                $*ERR.print(($part * 100).fmt(" % 3.2f%%"));
                my $est_time_left = (1 - $part) * (now - INIT now) / $part;
                $*ERR.print(($est_time_left / 60).fmt(" %d") ~ ($est_time_left % 60).fmt(":%02ds"));
            }
            $*ERR.print("\n");
        }
    }

    try mkdir "spesh_diffs_before";
    try mkdir "spesh_diffs_after";

    my Spesh $current;

    for lines(:eager) {
        my $line = $_;
        $linecount++;
        when /^'  ' / {
            given $target {
                when Before {
                    $current.beforelines.push: $line;
                    notegraph(".");
                }
                when Specialized {
                    $current.afterlines.push: $line;
                    notegraph(".");
                }
            }
        }
        when /^
              [$<kind>=I 'nserting logging for specialization of ' | $<kind>=F 'inished specialization of '] \'
                $<name>=[<-[\']>*] \'
                ' (cuid: ' $<cuid>=[<-[\)]>+] ')'
            / {
            my $cuid = $<cuid>;
            if $<kind> eq 'I' {
                # want to build a new one
                while %speshes{$cuid}:exists {
                    $cuid ~= "_";
                }

                $current .= new(name => $<name>.Str, cuid => $<cuid>.Str, diff => "");
                %speshes{$current.cuid} = $current;
            } elsif $<kind> eq 'F' {
                # want to find the last one added that exists.
                while %speshes{$cuid}:exists {
                    $cuid ~= '_';
                }
                # so we chop off a _ again
                $cuid = $cuid.substr(0, *-1);

                $current = %speshes{$cuid};

                warn "couldn't find a before-image for cuid $cuid" unless $current;

                $target = Specialized;
            }
            notegraph($<kind>);
        }
        when /^ 'Before:'/ {
            $target = Before;
        }
        when /^ 'After:'/ {
            $current.before = $current.beforelines.join("\n");
            $current.beforelines = @();
            $target = Logged;
        }
        when /^ 'Facts:'/ {
            if $target ~~ Specialized {
                given $current {
                    .after = .afterlines.join("\n");
                    .afterlines = @();
                    spurt "spesh_diffs_before/{.cuid}.txt", "{.name} (before)\n{.before}";
                    spurt "spesh_diffs_after/{.cuid}.txt", "{.name} (after)\n{.after}";
                    unless $matcher {
                        .before = "";
                        .after = "";
                    }
                }
            }
            $target = Facts;
        }
    }

    say "we've parsed $linecount lines";
    say "we have the following cuids:";

    my @results;
    my @interesting;

    for %speshes.values {
        next if not .after.defined;

        @results.push: $_.diff = qq:x"git diff --patience --color=always --no-index spesh_diffs_before/{.cuid}.txt spesh_diffs_after/{.cuid}.txt";
        my $matched = $matcher && $_ ~~ $ssm;
        @interesting.push: $_.diff if $matched;

        printf "%30s %s (%s)\n", .cuid, ($matched ?? "*" !! " "), .name;
    }

    for @interesting || @results {
        .say
    }
    if $matcher and 1 < @interesting < @results {
        note "matcher selected {+@interesting} out of {+@results} cuids";
    }
    if $matcher and 0 == @interesting {
        note "matcher matched no cuids"
    }
}
