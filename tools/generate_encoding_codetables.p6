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
        # Map unmapped things to identical codepoints
        if $Unicode_hex ~~ /^\s+/ {
            $Unicode_hex = $cp1252_hex;
        }
        $Unicode_hex ~~ s/^0x//;
        $cp1252_hex ~~ s/^0x//;
        %to-hex1252{$cp1252_hex.parse-base(16)} = $Unicode_hex.parse-base(16);
    }
    die unless elems %to-hex1252 == 256;
    %to-hex1252;
}
sub MAIN {
    my $DIR = "UNIDATA/CODETABLES";
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
    my $max = %to-hex1252.values.max;
    my $out_str2 = "static MVMuint8 {$encoding}_cp_to_char(MVMint32 codepoint) \{\n";
    my $out_str3 = qq:to/END/;
    if ($max < codepoint || codepoint < 0)
        return '\\0';
    switch (codepoint) \{
    END
    my @cases;
    for %to-hex1252.keys.sort({%to-hex1252{$^a} <=> %to-hex1252{$^b}}) -> $win_cp {
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
