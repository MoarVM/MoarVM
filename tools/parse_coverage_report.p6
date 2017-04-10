use v6;

sub MAIN(
    Str  $coverage where *.IO.e, # full-cover
    Str  $source   where *.IO.e, # gen/moar/CORE.setting
    Str :$annotations,  # setting
) {
    my (%annotations, %covered-lines)
    := |await (start get-annotations-from $annotations),
               start get-coverage-from    $coverage;

    my (%all, %stats is BagHash, $current-file, @lines, int $i);
    for $source.IO.lines -> $line {
        $i++;
        when $line.starts-with: '#line 1 SETTING::src' {
            $current-file
                andthen %all{$_} = process-stats %(|%stats), @lines
                andthen create-coverage-file $_, @lines;
            %stats = file => $current-file = $line.words.tail;
            @lines = ();
            $i = 0;
        }

        @lines.push: $_ => $line with do with $current-file {
            when so %covered-lines{$_}{~$i} { %stats<covered>++;   'c' }
            when so   %annotations{$_}{~$i} { %stats<uncovered>++; 'u' }
            %stats<uncovered>++; 'i';
        }
    }

    # also build a little overview page
    with "coverage/index.html".IO.open(:w) -> $outfile {
        LEAVE $outfile.close;
        $outfile.say: Q:c:to/TMPL/;
            <!DOCTYPE html>
            <html lang="en">
                <meta charset="utf-8">
                <title>coverage overview for {$source} </title>
                {$*css}
            TMPL

        $outfile.say: Q:to/TMPL/;
            <table id="coverage" class="sort">
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

        for %all.sort {
            my $name = .key;
            my $v    = .value;
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

        $outfile.say: Q:to/TMPL/;
                </tbody>
            </table>
            <script src="tablesort.js"></script>
            <script src="tablesort.number.js"></script>
            <script>new Tablesort(document.getElementById("coverage"));</script>
            TMPL
    }

    spurt 'coverage/tablesort.js', $*tablesort;
    spurt 'coverage/tablesort.number.js', $*tablesort-number;
}

sub process-stats ($_, @lines) {
    .<path>      = .<file>.subst(:g, /\W/, '_') ~ ".coverage.html";
    .<covered>   = +@lines.grep: *.key eq 'c';
    .<uncovered> = +@lines.grep: *.key eq 'u';
    .<ignored>   = +@lines.grep: *.key eq 'i';
    .<total>     = .<ignored> + .<covered> + .<uncovered>;
    with .<covered> + .<uncovered> -> $all {
        .<percentage> = ($all == 0 ?? 0 !! 100 * .<covered> / $all).round: .01
    }

    $_
}

sub create-coverage-file (%stats, @lines) {
    with "coverage/%stats<path>".IO {
        .spurt: join "\n",
            Q:h:to/TMPL/,
                <!DOCTYPE html>
                <html lang="en">
                    <meta charset="utf-8">
                    <title>coverage report for %stats<file> </title>
                <style>
                    li   { white-space: pre }
                    li.c { background: #cfc }
                    li.u { background: #fcc }
                    li.i { background: #ccc }
                    li:before {  background: #ccc; }
                </style>
                <body>
                    <ol style="font-family: monospace">
                TMPL
            @lines.map({
                '<li class="' ~ .key ~ '">' ~ .value.trans: ['<'] => ['&lt;']
            }),
            '</ol>';

        note "Wrote $_";
    }
}

multi get-annotations-from ($ann where .?IO.e) {
    note "Analyzing annotations file $ann";
    my %annotations .= push: $ann.IO.lines.grep(
        *.starts-with: '     annotation: SETTING::'
    ).map(*.substr: chars '     annotation: ').map: {
        # filename => line number
           substr($_, 0, rindex($_, ':')  )
        => substr($_,    rindex($_, ':')+1)
    }
    note "Done analyzing annotations file: {
        %annotations{*;}».elems.sum
    } lines found";
    %annotations».Set
}
multi get-annotations-from ($ann) {
    note Q:c:to/END/;
        No annotations file { $ann ?? "found at $ann" !! "supplied" }.
        This tool relies on a file generated with moar --dump foobar.moarvm
        that contains annotations for every line that can potentially be hit
        END
    %
}

sub get-coverage-from($file) {
    note "Reading coverage report from $file";
    my %coverage .= push: $file.IO.lines.grep(*.starts-with: 'HIT').map: {
        .[1] => .[2] with .words # filename => line number
    }

    note "Coverage report read: {%coverage{*;}».elems.sum} lines covered.";
    %coverage».Set
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
(function() {
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
