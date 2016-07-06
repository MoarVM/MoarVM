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
        $outfile.say: qq:to/TMPL/;
            <html><head>
                <title>coverage overview for $source </title>
                $*css
            </head>
        TMPL

        $outfile.say: qq:to/TMPL/;
            <table id="coverage">
                <thead>
                    <tr>
                        <th>Filename</th>
                        <th>Covered</th>
                        <th>Percentage</th>
                        <th>Uncovered</th>
                        <th>Ignored</th>
                        <th>Total</th>
                    </tr>
                </thead>
                <tbody>
        TMPL

        for %stats.sort {
            my $name = .key;
            my $v = .value;
            $outfile.say: qq:to/TMPL/;
                    <tr>
                        <td> <a href="$v<path>"> $name </a> </td>
                        <td> $v<covered> </td>
                        <td> $v<percentage>% </td>
                        <td> $v<uncovered> </td>
                        <td> $v<ignored> </td>
                        <td> $v<total> </td>
                    </tr>
            TMPL
        }

        $outfile.say: qq:to/TMPL/;
                </tbody>
            </table>
            <script type="text/javascript" src="tablesort.min.js"></script>
            <script type="text/javascript" src="tablesort.number.js"></script>
            <script>
                new Tablesort(document.getElementById("coverage"));
            </script>
        </body></html>
        TMPL

        $outfile.close;
    }

    spurt 'coverage/tablesort.min.js', $*tablesort;
    spurt 'coverage/tablesort.number.js', $*tablesort-number;
}

my $*css = q:to/CSS/;
    <style type="text/css">
        th.sort-header::-moz-selection { background:transparent; }
        th.sort-header::selection      { background:transparent; }
        th.sort-header { cursor:pointer; }
        table th.sort-header:after {
            content:'';
            float:right;
            margin-top:7px;
            border-width:0 4px 4px;
            border-style:solid;
            border-color:#404040 transparent;
            visibility:hidden;
        }
        table th.sort-header:hover:after {
            visibility:visible;
        }
        table th.sort-up:after,
        table th.sort-down:after,
        table th.sort-down:hover:after {
            visibility:visible;
            opacity:0.4;
        }
        table th.sort-up:after {
            border-bottom:none;
            border-width:4px 4px 0;
        }
    </style>
CSS

my $*tablesort = q:to/TABLESORT/;
/*!
 * tablesort v4.0.1 (2016-03-30)
 * http://tristen.ca/tablesort/demo/
 * Copyright (c) 2016 ; Licensed MIT
*/!function(){function a(b,c){if(!(this instanceof a))return new a(b,c);if(!b||"TABLE"!==b.tagName)throw new Error("Element must be a table");this.init(b,c||{})}var b=[],c=function(a){var b;return window.CustomEvent&&"function"==typeof window.CustomEvent?b=new CustomEvent(a):(b=document.createEvent("CustomEvent"),b.initCustomEvent(a,!1,!1,void 0)),b},d=function(a){return a.getAttribute("data-sort")||a.textContent||a.innerText||""},e=function(a,b){return a=a.toLowerCase(),b=b.toLowerCase(),a===b?0:b>a?1:-1},f=function(a,b){return function(c,d){var e=a(c.td,d.td);return 0===e?b?d.index-c.index:c.index-d.index:e}};a.extend=function(a,c,d){if("function"!=typeof c||"function"!=typeof d)throw new Error("Pattern and sort must be a function");b.push({name:a,pattern:c,sort:d})},a.prototype={init:function(a,b){var c,d,e,f,g=this;if(g.table=a,g.thead=!1,g.options=b,a.rows&&a.rows.length>0&&(a.tHead&&a.tHead.rows.length>0?(c=a.tHead.rows[a.tHead.rows.length-1],g.thead=!0):c=a.rows[0]),c){var h=function(){g.current&&g.current!==this&&(g.current.classList.remove("sort-up"),g.current.classList.remove("sort-down")),g.current=this,g.sortTable(this)};for(e=0;e<c.cells.length;e++)f=c.cells[e],f.classList.contains("no-sort")||(f.classList.add("sort-header"),f.tabindex=0,f.addEventListener("click",h,!1),f.classList.contains("sort-default")&&(d=f));d&&(g.current=d,g.sortTable(d))}},sortTable:function(a,g){var h,i=this,j=a.cellIndex,k=e,l="",m=[],n=i.thead?0:1,o=a.getAttribute("data-sort-method"),p=a.getAttribute("data-sort-order");if(i.table.dispatchEvent(c("beforeSort")),g?h=a.classList.contains("sort-up")?"sort-up":"sort-down":(h=a.classList.contains("sort-up")?"sort-down":a.classList.contains("sort-down")?"sort-up":"asc"===p?"sort-down":"desc"===p?"sort-up":i.options.descending?"sort-up":"sort-down",a.classList.remove("sort-down"===h?"sort-up":"sort-down"),a.classList.add(h)),!(i.table.rows.length<2)){if(!o){for(;m.length<3&&n<i.table.tBodies[0].rows.length;)l=d(i.table.tBodies[0].rows[n].cells[j]),l=l.trim(),l.length>0&&m.push(l),n++;if(!m)return}for(n=0;n<b.length;n++)if(l=b[n],o){if(l.name===o){k=l.sort;break}}else if(m.every(l.pattern)){k=l.sort;break}for(i.col=j,n=0;n<i.table.tBodies.length;n++){var q,r=[],s={},t=0,u=0;if(!(i.table.tBodies[n].rows.length<2)){for(q=0;q<i.table.tBodies[n].rows.length;q++)l=i.table.tBodies[n].rows[q],l.classList.contains("no-sort")?s[t]=l:r.push({tr:l,td:d(l.cells[i.col]),index:t}),t++;for("sort-down"===h?(r.sort(f(k,!0)),r.reverse()):r.sort(f(k,!1)),q=0;t>q;q++)s[q]?(l=s[q],u++):l=r[q-u].tr,i.table.tBodies[n].appendChild(l)}}i.table.dispatchEvent(c("afterSort"))}},refresh:function(){void 0!==this.current&&this.sortTable(this.current,!0)}},"undefined"!=typeof module&&module.exports?module.exports=a:window.Tablesort=a}();
TABLESORT

my $*tablesort-number = q:to/TABLESORT-NUMBER/;
(function(){
  var cleanNumber = function(i) {
    return i.replace(/[^\-?0-9.]/g, '');
  },

  compareNumber = function(a, b) {
    a = parseFloat(a);
    b = parseFloat(b);

    a = isNaN(a) ? 0 : a;
    b = isNaN(b) ? 0 : b;

    return a - b;
  };

  Tablesort.extend('number', function(item) {
    return item.match(/^-?[£\x24Û¢´€]?\d+\s*([,\.]\d{0,2})/) || // Prefixed currency
      item.match(/^-?\d+\s*([,\.]\d{0,2})?[£\x24Û¢´€]/) || // Suffixed currency
      item.match(/^-?(\d)*-?([,\.]){0,1}-?(\d)+([E,e][\-+][\d]+)?%?$/); // Number
  }, function(a, b) {
    a = cleanNumber(a);
    b = cleanNumber(b);

    return compareNumber(b, a);
  });
}());
TABLESORT-NUMBER

# vim: ft=perl6 expandtab sw=4
