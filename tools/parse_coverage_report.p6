use v6;

sub MAIN(Str $file where *.IO.e, Str $source where *.IO.e, Str $filename? = $source, Str :$annotations?) {
    my %covered-lines;

    my int $lineno = 0;

    my $parse_annotations = start {
        if $annotations.IO.e {
            note "analyzing annotations file";
            my %existing_lines;
            for $annotations.IO.lines {
                if /^ \s+ 'annotation: ' <-[:]>+ ':' (\d+) $/ {
                    %existing_lines<<$0>> = True;
                }
            }
            note "done analyzing annotations file: %existing_lines.elems() lines found";
            %existing_lines;
        } else {
            if $annotations {
                note "no annotations file found at $annotations.";
            } else {
                note "no annotations file supplied";
            }
            note "this tool relies on a file generated with moar --dump foobar.moarvm";
            note "that contains annotations for every line that can potentially be hit.";
        }
    }

    my $parse_source = start {
        note "analyzing source file for '#line N filename' lines.";
        my @file_ranges;
        for $source.IO.lines.kv -> $lineno, $_ {
            if $_.starts-with('#line ') {
                my @parts = .words;
                @file_ranges.push: [$lineno, +@parts[1], @parts[2]];
            }
        }
        note "finished analyzing source file. @file_ranges.elems() line/filename annotations found.";
        @file_ranges;
    }

    note "reading coverage report";
    for $file.IO.lines {
        next unless $_.starts-with("HIT");
        my @parts = .words;
        $lineno = $lineno + 1;
        next unless @parts == 3;
        #$*ERR.print: "." if $lineno %% 100;
        #$*ERR.print: "\n" if $lineno %% 8_000;
        next unless @parts[1].contains($filename);
        %covered-lines{@parts[2]} = 1;
    }
    note "coverage report read: %covered-lines.elems() lines covered.";

    my $highest-lineno = +([max] %covered-lines.keys);
    my $linenolen = $highest-lineno.chars + 1;
    my $format = "%0" ~ $linenolen ~ "d";

    my %existing_lines = await $parse_annotations;
    my @file_ranges    = await $parse_source;

    my $covered;
    my $uncovered;
    my $ignored;

    my $curfile = 1;
    my $current_source = $source;

    my @lines;

    my %stats;

    for $source.IO.lines.kv -> $num, $text {
        if $curfile < @file_ranges && @file_ranges[$curfile][0] == $num {
            $current_source = @file_ranges[$curfile-1][2];
            my $path = "coverage/{$current_source.subst("/", "_", :g)}.coverage.html";
            my $outfile = $path.IO.open(:w);
            $outfile.say(make_html($source));
            $outfile.say(@lines.join("\n"));
            $outfile.say(finish_html);
            $outfile.close;

            say "line $num; wrote $path";
            %stats{$current_source}<covered>    = $covered;
            %stats{$current_source}<uncovered>  = $uncovered;
            %stats{$current_source}<ignored>    = $ignored;
            %stats{$current_source}<percentage> = 100 * $covered / ($covered + $uncovered);
            %stats{$current_source}<total>      = $covered + $uncovered + $ignored;
            %stats{$current_source}<path>       = $path.subst("coverage/", "");

            $covered = 0;
            $uncovered = 0;
            $ignored = 0;

            @lines = ();
            $curfile++;
        }
        my $class = %covered-lines{$num + 1} ?? "c" !! "u";
        if $class eq "u" && %existing_lines && not %existing_lines{$num + 1}:exists {
            $class = "i";
            $ignored++;
        } elsif $class eq "u" {
            $uncovered++;
        } elsif $class eq "c" {
            $covered++;
        }
        @lines.push: "<li class=\"$class\">$text.subst("<", "&lt;", :g)"; # skip the </li> because yay html!
    }

    sub make_html($sourcefile) {
        qq:to/TMPL/;
            <html><head><title>coverage report for $source </title></head>
            <style>
                li   \{ white-space: pre }
                li.c \{ background: #cfc }
                li.u \{ background: #fcc }
                li.i \{ background: #ccc }
                li:before \{  background: #ccc; }
            </style>
            <body>
                <ol style="font-family: monospace">
            TMPL
    }

    sub finish_html() {
        qq:to/TMPL/;
                </ol>
            </body>
            </html>
            TMPL
    }
    
    # also build a little overview page

    {
        my $outfile = "coverage/index.html".IO.open(:w);
        $outfile.say: " <html><head><title>coverage overview for $source </title></head>";

        $outfile.say: qq:to/TMPL/;
            <table>
                <tr>
                    <th>Filename</th>
                    <th>Covered</th>
                    <th>Uncovered</th>
                    <th>Ignored</th>
                    <th>Total</th>
                </tr>
        TMPL

        for %stats.sort {
            my $name = .key;
            my $v = .value;
            $outfile.say: qq:to/TMPL/;
                <tr>
                    <td><a href="$v<path>"> $name </a></td>
                    <td> $v<covered> ($v<percentage>%) </td>
                    <td> $v<uncovered> </td>
                    <td> $v<ignored> </td>
                    <td> $v<total> </td>
                </tr>
            TMPL
        }

        $outfile.say: "</table></body></html>";

        $outfile.close;
    }
}
