sub usage {
    print q:to^USAGE^;
        This tool lets you create blobs that you can concatenate to the
        special concatmoar binary to create stand-alone packages containing
        moarvm bytecode files as well as libraries that contain extension ops.

        Multiple of these blobs can be concatenated into the same file. It's
        meant to allow a rakudo standalone to be built and then extended with
        any user script, for example.

        It accepts triples of arguments: a path & filename, a type, and the
        path & filename to the file to include.

        The type can be any of these:

            mbc     a .moarvm file that will be loaded on startup
            dll     a dll that will be opened on startup

        The third argument may also be passed as - to mean that stdin will be
        read for this entry.

        The finished blob will be printed to stdout. A little summary of
        the contents of the blob will be printed to stderr.
        USAGE
}

sub int64_to_buf($num) {
    Buf.new(flat(0 xx 8, $num.polymod(256 xx *).reverse)[*-8..*].reverse)
}

sub write_postlude($size) {
    my $greeting = "This file contains extra stuff. Size: ";
    $*OUT.print($greeting);
    $*OUT.write(int64_to_buf($size));
    $*OUT.print("!" x (64 - $greeting.chars - 8));
}

unless @*ARGS %% 3 && @*ARGS {
    usage;
    exit 0
}

for @*ARGS.rotor(3 => 0).kv -> $index, ($targetname, $kind, $sourcefile) {
    die "kind $kind not understood in triplet $index" unless $kind.lc eq one <mbc dll>;
    die "file $sourcefile couldn't be found" unless $sourcefile eq '-' || $sourcefile.IO.f;
}

my @pieces;
my $sumsize;

my $subheadercookie = "SUBFILE:".encode("utf8");

for @*ARGS.rotor(3 => 0).kv -> $index, ($targetname, $kind, $sourcefile) {
    note "File $index: $sourcefile; installed as a $kind named $targetname";
    my $blob = do if $sourcefile eq '-' {
        $*IN.slurp-rest(:bin);
    } else {
        $sourcefile.IO.open(:r).slurp-rest(:bin)
    }
    note "read $blob.bytes() bytes";

    my $nameblob = $targetname.encode("utf8") ~ Blob.new(0);

    @pieces.push(my $result = $subheadercookie ~ $nameblob ~ int64_to_buf($blob.bytes) ~ $kind.lc.encode("utf8") ~ $blob);
    $sumsize += $result.bytes;
}

note "in total, a blob of $sumsize bytes will be written";

$*OUT.write($_) for @pieces;
write_postlude($sumsize);
