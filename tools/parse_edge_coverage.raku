my %id_to_frame;
my %frame_to_ids;
my %id_to_fnl;
my %id_to_fname;
my %fnl_to_frames;

my %incoming;
my %outgoing;
my %frame_incoming;
my %frame_outgoing;
my %intra_frame_paths;

my $rakudo_prefix = $*VM.config<prefix>;
my $nqplib_prefix = $rakudo_prefix ~ "/share/nqp/lib";

sub SHRT($_) {
    $_.subst($nqplib_prefix, '$NQPLIB').subst($rakudo_prefix, '$PREFIX')
}

for lines() -> $lin {
    my @bits = $lin.split(":");
    if @bits[0] eq "EHIT" || @bits[0] eq "CHIT" {
        # skip very first edge which comes from 0
        next if @bits[1] eq "0";

        %incoming{@bits[2]}.push(@bits[1]);
        %outgoing{@bits[1]}.push(@bits[2]);
        my $from_frame = %id_to_frame{@bits[1]};
        my $to_frame   = %id_to_frame{@bits[2]};
        if $from_frame[0] ne $to_frame[0] {
            %frame_incoming{$to_frame[0]}.push(  $($from_frame[0], $from_frame[1], $to_frame[1],   @bits[1], @bits[2]));
            %frame_outgoing{$from_frame[0]}.push($($to_frame[0],   $to_frame[1],   $from_frame[1], @bits[1], @bits[2]));
        }
        else {
            %intra_frame_paths{$from_frame[0]}.push($($from_frame[1], $to_frame[1]));
        }
    }
    elsif @bits[0] eq "BBI" {
        my $frame = SHRT(@bits[1]) ~ "[" ~ @bits[2] ~ "]";
        %frame_to_ids{$frame}.push($(+@bits[3], +@bits[4]));
        %id_to_frame{@bits[4]} = $($frame, +@bits[3]);
    }
    elsif @bits[0] eq "FNL" {
        my $frame = %id_to_frame{SHRT(@bits[1])};
        %id_to_fnl{$frame[0]}{$frame[1]} = my $fnl = SHRT(@bits.skip(3).join(":")) ~ ":" ~ @bits[2];
        %fnl_to_frames{$fnl}.push($frame[0]);
    }
    elsif @bits[0] eq "FN" {
        %id_to_fname{%id_to_frame{@bits[1]}[0]} = @bits.skip(2).join(":");
    }
}

#say "frames with the most outgoing edges:";
#for %frame_outgoing.sort(*.value.elems).tail(10) {
#    say .key;
#    for .value.list.sort {
#        say "    $_";
#    }
#}

say Q:to/HTML/;
<!DOCTYPE html>
<html>
 <style>
  body {
    white-space: pre-wrap;
    font-family: monospace;
  }
  li {
    white-space: initial;
  }
  :target {
    border: 1px solid grey;
  }
 </style>
<body>
HTML

sub escapy_frame($_) {
    .trans(["/", "#",  "[",  "]", "&", "?", "\$", '"', ":"] =>
           ["S", "H", "LB", "RB", "A", "Q", "D", "DQ", "C"]);
}
sub linky_frame($_, $text) {
    "<a href=\"#&escapy_frame($_)\">$text\</a>"
}
sub htmlify($_) {
    .trans(["&", "<"] => ['&amp;', '&lt;'])
}

say Q:to/HTML/;
<div style="float:right"><ul>
HTML

for %fnl_to_frames.categorize(*.key.split(":").head(*-1).join(":")).sort {
    say "<li>$_.key()\<ul>";
    for .value.list.sort({.key.split(":").head(*-1), .key.split(":")[*-1].Int }) {
        my $clickables = .value.list.map({ linky_frame $_, $_ }).join(", ");
        say "<li id=\"&escapy_frame(.key)\">$_.key.split(":").tail() $clickables\</li>";
    }
    say "</ul></li>";
}

say Q:to/HTML/;
</ul></div>
HTML

for %intra_frame_paths.sort({ .key.split("[").head, +.key.split("[").tail.chop }) {
    my $filename_only = .key.split("[").head;
    if (state $prev_filename = "") ne $filename_only {
        say "<h1 id=\"&escapy_frame($filename_only)\">$_.key.split('[').head()\</h1>";
        $prev_filename = $filename_only;
    }
    say "<h2 id=\"&escapy_frame(.key)\">$_.key()", |("      \"&htmlify($_)\"" with %id_to_fname{.key}), "</h2>";
    my @bb_list;
    with %id_to_fnl{.key} -> %fn_ann {
        for %fn_ann {
            @bb_list.push($( (+.key, "A"), "$_.key.fmt("% 3s") ... ... <a href=\"#&escapy_frame(.value)\">$_.value()\</a>" ));
        }
    }
    with %frame_outgoing{.key} {
        for @$_ {
            my $frame_with_name = .[0] ~ ("    \"&htmlify($_)\"" with %id_to_fname{.[0]});
            $frame_with_name = linky_frame(.[0], $frame_with_name);
            @bb_list.push($( (+.[2], "X"), .[2].Int.fmt("% 3s"), "      ", "->", $frame_with_name, "BB " ~ .[1]));
        }
    }
    with %frame_incoming{.key} {
        for @$_ {
            my $frame_with_name = .[0] ~ ("    \"&htmlify($_)\"" with %id_to_fname{.[0]});
            $frame_with_name = linky_frame(.[0], $frame_with_name);
            @bb_list.push($((+.[2], "D"), .[2].Int.fmt("% 3s"), "      ", "<-", $frame_with_name, "BB " ~ .[1]));
        }
    }
    @bb_list.push(|.value.list.map({ $((+.[0], "M"), .[0].Int.fmt("% 3s"), "->", .[1].fmt("% 3s")) }));
    for @bb_list.sort.unique {
        say "    $_.skip(1)";
    }
    say "";
}

say Q:to/HTML/;
</body>
HTML
