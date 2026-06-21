#!/usr/bin/env raku
# Gets the latest Unicode Data files and extracts them.
use v6;
my $unicode-ftp = "ftp://ftp.unicode.org/Public";
my $UCD-zip     = "$unicode-ftp/UCD/latest/ucd/UCD.zip";
my $UCA-allkeys = "$unicode-ftp/UCA/latest/allkeys.txt";
my $UCA-test    = "$unicode-ftp/UCA/latest/CollationTest.zip";
my $MAPPINGS    = "$unicode-ftp/MAPPINGS";
my @CODETABLES  =
    'VENDORS/MICSFT/WINDOWS/CP1252.TXT',
    'VENDORS/MICSFT/WINDOWS/CP1251.TXT';
my $JIS-url     = "https://encoding.spec.whatwg.org/index-jis0208.txt";

sub MAIN(Bool :$allow-draft) {
    quit "Must run in the top level of a checked-out MoarVM git repo."
        unless '.git'.IO.d;

    my IO::Path $unidata = 'UNIDATA'.IO.absolute.IO;
    quit "$unidata directory already exists. Please delete it and run again."
        if $unidata.e;

    say "Creating new UNIDATA directory";
    mkdir $unidata;

    indir $unidata, {
        download-zip-file($UCD-zip);
        download-zip-file($UCA-test, 'UCA');
        indir 'UCA', { download-files($UCA-allkeys) };

        say "\nDownloading codetables from $MAPPINGS";
        mkdir 'CODETABLES';
        indir 'CODETABLES', {
            download-files("$MAPPINGS/$_") for @CODETABLES;
            download-files($JIS-url);
        }

        get-emoji(:$allow-draft);
    }
}

sub quit($message) {
    note $message;
    exit;
}

sub read-url($url) {
    qqx{curl --ftp-method nocwd -s "$url"}
}

sub ftp-dir-entries($url) {
    read-url($url).lines.map(*.split(/' '+/)[8])
}

sub download-files(+@urls) {
    if @urls == 1 {
        my $filename = @urls[0].subst(/^.*\//, '');
        say "\nDownloading $filename from @urls[0]";
    }
    temp %*ENV<COLUMNS> = 80;
    ?run < curl -# --ftp-method nocwd --remote-name-all >, |@urls;
}

sub download-zip-file(Str:D $url, Str:D $dir = '.') {
    unless $dir.IO.d {
        say "\nCreating the $dir subdirectory";
        mkdir $dir;
    }

    indir $dir, {
        my $filename = $url.subst(/^.*\//, '');
        download-files($url);
        run 'unzip', $filename;
    }
}

sub get-emoji(Bool :$allow-draft) {
    # Since emoji sequence names are not canonical and unchangeable, we get all
    # of them starting with the first the feature was added in.  Directory layout
    # changed as of versions 13.0 and 17.0, so handle before/after separately.
    my $first-emoji-ver = v4.0;
    my $data-moved-ver  = v13.0;
    my $reorg-emoji-ver = v17.0;

    say "\nGetting a listing of available OLD emoji versions (< $reorg-emoji-ver)";
    my @old-list     = ftp-dir-entries($unicode-ftp ~ '/emoji/');
    my @old-versions = @old-list.grep(/^\d/).map({Version.new($_)})
                        .grep($first-emoji-ver <= *).sort;
    my @downloads    = @old-versions.map({ $_ => "$unicode-ftp/emoji/$_" });
    say "OLD emoji versions found: @old-versions[]";

    say "\nGetting a listing of available NEW emoji versions (>= $reorg-emoji-ver)";
    my @new-list     = ftp-dir-entries($unicode-ftp ~ '/');
    my @new-versions = @new-list.grep(/^\d/).map({Version.new($_)})
                                .grep($reorg-emoji-ver <= *).sort;
    @downloads.append: @new-versions.map({ $_ => "$unicode-ftp/$_/emoji" });
    say "NEW emoji versions found: @new-versions[]";

    my @new-file-list = < ReadMe.txt emoji-sequences.txt
                          emoji-zwj-sequences.txt emoji-test.txt >;
    my @old-file-list = |@new-file-list, 'emoji-data.txt';

    for @downloads -> (:key($version), :value($emoji-data-url)) {
        put "\nEmoji version $version:";
        my $readme = read-url("$emoji-data-url/ReadMe.txt").chomp;
        if $readme.match(/draft|PRELIMINARY/, :i) {
            say "Looks like this version is a draft. ReadMe.txt text: <<$readme>>";
            next unless $allow-draft;
        }

        my @files = $version >= $data-moved-ver ?? @new-file-list
                                                !! @old-file-list;
        my @urls  = @files.map({ "$emoji-data-url/$_" });
        my $emoji-folder = "emoji-$version".IO;
        say "Fetching @files[] to $emoji-folder";

        $emoji-folder.mkdir;
        indir $emoji-folder, { download-files(@urls) };
    }
}
