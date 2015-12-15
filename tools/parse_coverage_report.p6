use v6;

sub MAIN(Str $file where *.IO.e, Str $source where *.IO.e, Str $filename? = $source) {
    my %covered-lines;

    my int $lineno = 0;

    for $file.IO.lines {
        my @parts = .words;
        $lineno = $lineno + 1;
        next unless @parts == 3;
        next unless @parts[0] eq "HIT";
        #$*ERR.print: "." if $lineno %% 100;
        #$*ERR.print: "\n" if $lineno %% 8_000;
        next unless @parts[1].contains($filename);
        %covered-lines{@parts[2]} = 1;
    }

    my $highest-lineno = +([max] %covered-lines.keys);
    my $linenolen = $highest-lineno.chars + 1;
    my $format = "%0" ~ $linenolen ~ "d";

    say qq:to/TMPL/;
        <html><head><title>coverage report for $source </title></head>
        <style>
            li   \{ white-space: pre }
            li.c \{ background: #cfc }
            li.u \{ background: #fcc }
            li:before \{  background: #ccc; }
        </style>
        <body>
            <ol style="font-family: monospace">
        TMPL
    for $source.IO.lines.kv -> $num, $text {
        my $class = %covered-lines{$num + 1} ?? "c" !! "u";
        say "<li class=\"$class\">$text.subst("<", "&lt;", :g)"; # skip the </li> because yay html!
    }

    say qq:to/TMPL/;
            </ol>
        </body>
        </html>
        TMPL
}
