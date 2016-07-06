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
            <script type="text/javascript" src="tablesort.js"></script>
            <script type="text/javascript" src="tablesort.number.js"></script>
            <script>
                new Tablesort(document.getElementById("coverage"));
            </script>
        </body></html>
        TMPL

        $outfile.close;
    }

    spurt 'coverage/tablesort.js', $*tablesort;
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
*/!
;(function() {
  function Tablesort(el, options) {
    if (!(this instanceof Tablesort)) return new Tablesort(el, options);

    if (!el || el.tagName !== 'TABLE') {
      throw new Error('Element must be a table');
    }
    this.init(el, options || {});
  }

  var sortOptions = [];

  var createEvent = function(name) {
    var evt;

    if (!window.CustomEvent || typeof window.CustomEvent !== 'function') {
      evt = document.createEvent('CustomEvent');
      evt.initCustomEvent(name, false, false, undefined);
    } else {
      evt = new CustomEvent(name);
    }

    return evt;
  };

  var getInnerText = function(el) {
    return el.getAttribute('data-sort') || el.textContent || el.innerText || '';
  };

  // Default sort method if no better sort method is found
  var caseInsensitiveSort = function(a, b) {
    a = a.toLowerCase();
    b = b.toLowerCase();

    if (a === b) return 0;
    if (a < b) return 1;

    return -1;
  };

  // Stable sort function
  // If two elements are equal under the original sort function,
  // then there relative order is reversed
  var stabilize = function(sort, antiStabilize) {
    return function(a, b) {
      var unstableResult = sort(a.td, b.td);

      if (unstableResult === 0) {
        if (antiStabilize) return b.index - a.index;
        return a.index - b.index;
      }

      return unstableResult;
    };
  };

  Tablesort.extend = function(name, pattern, sort) {
    if (typeof pattern !== 'function' || typeof sort !== 'function') {
      throw new Error('Pattern and sort must be a function');
    }

    sortOptions.push({
      name: name,
      pattern: pattern,
      sort: sort
    });
  };

  Tablesort.prototype = {

    init: function(el, options) {
      var that = this,
          firstRow,
          defaultSort,
          i,
          cell;

      that.table = el;
      that.thead = false;
      that.options = options;

      if (el.rows && el.rows.length > 0) {
        if (el.tHead && el.tHead.rows.length > 0) {
          firstRow = el.tHead.rows[el.tHead.rows.length - 1];
          that.thead = true;
        } else {
          firstRow = el.rows[0];
        }
      }

      if (!firstRow) return;

      var onClick = function() {
        if (that.current && that.current !== this) {
          that.current.classList.remove('sort-up');
          that.current.classList.remove('sort-down');
        }

        that.current = this;
        that.sortTable(this);
      };

      // Assume first row is the header and attach a click handler to each.
      for (i = 0; i < firstRow.cells.length; i++) {
        cell = firstRow.cells[i];
        if (!cell.classList.contains('no-sort')) {
          cell.classList.add('sort-header');
          cell.tabindex = 0;
          cell.addEventListener('click', onClick, false);

          if (cell.classList.contains('sort-default')) {
            defaultSort = cell;
          }
        }
      }

      if (defaultSort) {
        that.current = defaultSort;
        that.sortTable(defaultSort);
      }
    },

    sortTable: function(header, update) {
      var that = this,
          column = header.cellIndex,
          sortFunction = caseInsensitiveSort,
          item = '',
          items = [],
          i = that.thead ? 0 : 1,
          sortDir,
          sortMethod = header.getAttribute('data-sort-method'),
          sortOrder = header.getAttribute('data-sort-order');

      that.table.dispatchEvent(createEvent('beforeSort'));

      // If updating an existing sort `sortDir` should remain unchanged.
      if (update) {
        sortDir = header.classList.contains('sort-up') ? 'sort-up' : 'sort-down';
      } else {
        if (header.classList.contains('sort-up')) {
          sortDir = 'sort-down';
        } else if (header.classList.contains('sort-down')) {
          sortDir = 'sort-up';
        } else if (sortOrder === 'asc') {
          sortDir = 'sort-down';
        } else if (sortOrder === 'desc') {
          sortDir = 'sort-up';
        } else {
          sortDir = that.options.descending ? 'sort-up' : 'sort-down';
        }

        header.classList.remove(sortDir === 'sort-down' ? 'sort-up' : 'sort-down');
        header.classList.add(sortDir);
      }

      if (that.table.rows.length < 2) return;

      // If we force a sort method, it is not necessary to check rows
      if (!sortMethod) {
        while (items.length < 3 && i < that.table.tBodies[0].rows.length) {
          item = getInnerText(that.table.tBodies[0].rows[i].cells[column]);
          item = item.trim();

          if (item.length > 0) {
            items.push(item);
          }

          i++;
        }

        if (!items) return;
      }

      for (i = 0; i < sortOptions.length; i++) {
        item = sortOptions[i];

        if (sortMethod) {
          if (item.name === sortMethod) {
            sortFunction = item.sort;
            break;
          }
        } else if (items.every(item.pattern)) {
          sortFunction = item.sort;
          break;
        }
      }

      that.col = column;

      for (i = 0; i < that.table.tBodies.length; i++) {
        var newRows = [],
            noSorts = {},
            j,
            totalRows = 0,
            noSortsSoFar = 0;

        if (that.table.tBodies[i].rows.length < 2) continue;

        for (j = 0; j < that.table.tBodies[i].rows.length; j++) {
          item = that.table.tBodies[i].rows[j];
          if (item.classList.contains('no-sort')) {
            // keep no-sorts in separate list to be able to insert
            // them back at their original position later
            noSorts[totalRows] = item;
          } else {
            // Save the index for stable sorting
            newRows.push({
              tr: item,
              td: getInnerText(item.cells[that.col]),
              index: totalRows
            });
          }
          totalRows++;
        }
        // Before we append should we reverse the new array or not?
        // If we reverse, the sort needs to be `anti-stable` so that
        // the double negatives cancel out
        if (sortDir === 'sort-down') {
          newRows.sort(stabilize(sortFunction, true));
          newRows.reverse();
        } else {
          newRows.sort(stabilize(sortFunction, false));
        }

        // append rows that already exist rather than creating new ones
        for (j = 0; j < totalRows; j++) {
          if (noSorts[j]) {
            // We have a no-sort row for this position, insert it here.
            item = noSorts[j];
            noSortsSoFar++;
          } else {
            item = newRows[j - noSortsSoFar].tr;
          }

          // appendChild(x) moves x if already present somewhere else in the DOM
          that.table.tBodies[i].appendChild(item);
        }
      }

      that.table.dispatchEvent(createEvent('afterSort'));
    },

    refresh: function() {
      if (this.current !== undefined) {
        this.sortTable(this.current, true);
      }
    }
  };

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = Tablesort;
  } else {
    window.Tablesort = Tablesort;
  }
})();
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
