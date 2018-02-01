#!/usr/bin/env perl6
# This code generates encoding tables for single byte encodings.
# Currently Windows-1252 and Windows-1251
sub make-windows-codepoints-table (Str:D $filename, Str:D $encoding, Str:D $comment) {
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
    say $out_str;
    die unless elems %to-hex1252 == 256;
}
my $DIR = "UNIDATA/CODETABLES";
make-windows-codepoints-table("$DIR/CP1252.TXT", "windows1252", "/* Windows-1252 Latin */");
make-windows-codepoints-table("$DIR/CP1251.TXT", "windows1251", "/* Windows-1251 Cyrillic */");
