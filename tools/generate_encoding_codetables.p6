#!/usr/bin/env perl6
# This code generates encoding tables for single byte encodings.
# Currently Windows-1252 and Windows-1251
sub process-file (Str:D $filename, Str:D $encoding) {
    my %to-hex1252;
    for $filename.IO.slurp.lines -> $line {
        next if $line.starts-with: '#';
        my ($cp1252_hex, $Unicode_hex, $comment) = $line.split: /\t/;
        if (!$cp1252_hex || !$Unicode_hex || !$comment) {
            die "'$cp1252_hex' '$Unicode_hex' '$comment'";
        }
        # Map unmapped things to 0xFFFF, so we can throw if we see them
        if $Unicode_hex ~~ /^\s+/ {
            $Unicode_hex = "0xFFFF";
        }
        $Unicode_hex ~~ s/^0x//;
        $cp1252_hex ~~ s/^0x//;
        %to-hex1252{$cp1252_hex.parse-base(16)} = $Unicode_hex.parse-base(16);
    }
    die unless elems %to-hex1252 == 256;
    %to-hex1252;
}
sub process-shift-jis-index (Str:D $filename) {
    my %indexes;
    my %unis;
    for $filename.IO.slurp.lines -> $line {
        next if $line ~~ /^\s*$/ || $line ~~ /^\s*'#'/;
        my ($index, $uni) = $line.split(/\s+/, :skip-empty);
        # Let index be index jis0208 excluding all entries whose pointer is in the range 8272 to 8835, inclusive.
        next if 8272 <= $index && $index <= 8835;
        my $uni_int = $uni.subst(/^0x/, "").parse-base(16);
        note "index $index already exists with codepoint %indexes{$index}. Adding for codepoint $uni" if %indexes{$index}:exists;
        # The index pointer for code point in index is the first pointer corresponding to code point in index, or null if code point is not in index.
        %indexes{$index} = $uni_int;
        if %unis{$uni_int}:!exists {
            %unis{$uni_int} = $index;
        }
    }
    my @cp_to_index;
    my @index_to_cp;
    my @index_to_cp_array;
    for %unis.sort(*.key.Int) -> $pair {
        my ($uni, $index) = ($pair.kv);
        push @cp_to_index, make-case($uni.fmt("0x%X"), $index);
    }
    @cp_to_index.push: "default: return 0;";
    my $last_seen_index = -1;
    for %indexes.sort(*.key.Int) -> $pair {
        my ($index, $uni) = ($pair.kv);
        #say "lastseen $last_seen_index $index uni $uni";
        if $last_seen_index+1 != $index {
            note "last seen was $last_seen_index. current is $index";
            note "if (index > $last_seen_index) counter += " ~ ($index - $last_seen_index - 1);

        }
        $last_seen_index = $index;
        #say "set it to $index";
        push @index_to_cp, make-case($index, $uni.fmt("0x%X"));
    }
    @index_to_cp.push: "default: return 0;";
    my $cp_to_index_str = "MVMuint16 shift_jis_cp_to_index (MVMCodepoint codepoint) \{\n" ~
        ("switch (codepoint) \{\n" ~ @cp_to_index.join("\n").indent(4) ~ "\n}").indent(4) ~
        "\n\}\n";
    my $index_to_cp_str = "MVMCodepoint shift_jis_index_to_cp (MVMuint16 index) \{\n" ~
    ("switch (index) \{\n" ~ @index_to_cp.join("\n").indent(4) ~ "\n}").indent(4) ~
    "\n\}\n";
    # Don't use $index_to_cp_str except for development. We will make an array
    # instead since are sequential, unlike mapping the opposite direction
    $index_to_cp_str ~ "\n" ~
    $cp_to_index_str;

}
sub MAIN {
    my $DIR = "UNIDATA/CODETABLES";
    say process-shift-jis-index("$DIR/index-jis0208.txt");
    my @info = %(encoding => 'windows1252', filename => "$DIR/CP1252.TXT", comment => "/* Windows-1252 Latin */"),
        %( encoding => 'windows1251', filename => "$DIR/CP1251.TXT", comment => "/* Windows-1251 Cyrillic */");
    my %win1252 = process-file(@info[0]<filename>, @info[0]<encoding>);
    my %win1251 = process-file(@info[1]<filename>, @info[1]<encoding>);

    say create-windows1252_codepoints(%win1252, @info[0]<encoding>, @info[0]<comment>);
    say create-windows1252_codepoints(%win1251, @info[1]<encoding>, @info[1]<comment>);
    say create-windows1252_cp_to_char(%win1252, @info[0]<encoding>);
    say create-windows1252_cp_to_char(%win1251, @info[1]<encoding>);
}
sub create-windows1252_codepoints (%to-hex1252, $encoding, $comment) {
    sub make_line (@lines, @out) {
        if @lines {
            my Str:D $out = join(",", @lines);
            @out.push: $out;
            @lines = Empty;
        }
    }
    my @lines;
    my $count = 0;
    my @out;
    for 0..255 {
        push @lines, "0x%04X".sprintf(%to-hex1252{$_});
        make_line @lines, @out if @lines %% 8;
    }
    make_line @lines, @out;
    my $out_str = "$comment\n" ~ "static const MVMuint16 {$encoding}_codepoints[] = \{\n" ~ @out.join(",\n").indent(4) ~ "\n\};";
    $out_str;

}
sub create-windows1252_cp_to_char (%to-hex1252, $encoding) {
    my $max = %to-hex1252.values.grep({$_ != 0xFFFF}).max;
    my $out_str2 = "static MVMuint8 {$encoding}_cp_to_char(MVMint32 codepoint) \{\n";
    my $out_str3 = qq:to/END/;
    if ($max < codepoint || codepoint < 0)
        return '\\0';
    switch (codepoint) \{
    END
    my @cases;
    for %to-hex1252.keys.sort({%to-hex1252{$^a} <=> %to-hex1252{$^b}}) -> $win_cp {
        next if %to-hex1252{$win_cp} == 0xFFFF;
        # Skip codepoints from 0..127 since those are in ASCII and don't need to
        # be in the switch
        next if $win_cp <= 127;
        @cases.push: make-case %to-hex1252{$win_cp}, $win_cp;
    }
    @cases.push: ‘default: return '\0';’;
    my $indent = ' ' x 4;
    $out_str2 ~= ($out_str3 ~ $indent ~ @cases.join("\n$indent") ~ "\n\};").indent(4) ~ "\n\}";
    $out_str2;
}
sub make-case (Cool:D $from, Cool:D $to) {
    "case $from: return $to;"
}
