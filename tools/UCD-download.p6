#!/usr/bin/env perl6
# Gets the latest Unicode Data files and extracts them.
use v6;
my $UCD-zip-lnk = "ftp://ftp.unicode.org/Public/UCD/latest/ucd/UCD.zip";
my $UCA-all-keys = "ftp://ftp.unicode.org/Public/UCA/latest/allkeys.txt";
my $UCA-collation-test = "ftp://ftp.unicode.org/Public/UCA/latest/CollationTest.zip";
sub download-file ( Str:D $url, Str:D $filename ) {
    qqx{curl "$url" -o "$filename"};
}
sub download-set-file ( Str:D $url, Str:D $filename, Str:D $dir) {
    if ! so "$dir/$filename".IO.f {
        my $cwd = $*CWD;
        say "Downloading $filename from $url";
        chdir $dir.IO;
        download-file($url, $filename);
        chdir $cwd;
    }
    if $filename.ends-with('.zip') {
        my $cwd = $*CWD;
        chdir $dir.IO;
        unzip-file($filename);
        chdir $cwd;
    }
}
sub unzip-file ( Str:D $zip ) {
    qqx{unzip "$zip"};
}
if ! so './UNIDATA'.IO.d {
    say "Creating UNIDATA directory";
    mkdir "UNIDATA";
}
chdir "UNIDATA";
if ! so "./UCD.zip".IO.f {
    say "Downloading the latest UCD from $UCD-zip-lnk";
    download-file($UCD-zip-lnk,"UCD.zip");
    say "Unzipping UCD.zip";
    unzip-file("UCD.zip");
}
if ! so "UCA".IO.d {
    say "Creating the UCA directory";
    mkdir "UCA";
    download-set-file($UCA-collation-test, 'CollationTest.zip', "UCA");
}
if ! so "./UCA/allkeys.txt".IO.f {
    say "Downloading allkeys.txt from $UCA-all-keys";
    chdir "UCA".IO;
    download-file($UCA-all-keys, "allkeys.txt");
    chdir '..';
}
my $emoji-dir = "ftp://ftp.unicode.org/Public/emoji/";
my @emoji-vers;
say "Getting a listing of the Emoji versions";
for qqx{curl -s "$emoji-dir"}.lines {
    push @emoji-vers, .split(/' '+/)[8];
}
for @emoji-vers.sort.reverse -> $version {
    say "See version $version of Emoji, checking to see if it's a draft";
    my $readme = qqx{curl -s "ftp://ftp.unicode.org/Public/emoji/$version/ReadMe.txt"}.chomp;
    if $readme.match(/draft|PRELIMINARY/, :i) {
        say "Looks like $version is a draft. ReadMe.txt text: <<$readme>>";
        next;
    }
    else {
        say "Found version $version. Don't see /:i draft|PRELIMINARY/ in the text.";
        my $emoji-data = "ftp://ftp.unicode.org/Public/emoji/$version/";
        say $emoji-data;
        mkdir "emoji";
        chdir "emoji";
        download-file("$emoji-data/ReadMe.txt", "ReadMe.txt");
        download-file("$emoji-data/emoji-data.txt", "emoji-data.txt");
        download-file("$emoji-data/emoji-sequences.txt", "emoji-sequences.txt");
        download-file("$emoji-data/emoji-zwj-sequences.txt", "emoji-zwj-sequences.txt");
        chdir "..";
        last;
    }
}
