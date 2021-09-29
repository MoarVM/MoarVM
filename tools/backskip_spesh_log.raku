multi sub partition(Str:D $filename) {
    partition($filename.IO.open(:r, :enc<utf8>));
}

multi sub partition(IO::Handle:D $file) {
    my $f = $file;

    $f.seek(-4096, SeekFromEnd);

    my @positions;

    my $start_position = 0;
    loop {
        my @results = (my $piece = $f.read(4096).decode("utf8")).comb(/^^skip\:\d+$$/);
        if @results {
            $start_position = @results.tail.comb(/\d+/).head.Int;
            last;
        }
        $f.seek(-4096 * 2 + 64, SeekFromCurrent);
    }

    my $position = $start_position;
    while $position > 0 {
        @positions.push: $position;
        $f.seek($position max 0, SeekFromBeginning);
        my $piece = $f.read(64).decode("utf8");
        unless $piece.starts-with("skip:") { die "expected piece starting at $position to start with `skip:`, but piece gotten was `$piece`" }
        $position = $piece.substr("skip:".chars, $piece.index("\n") - "skip:".chars).Int;
    }

    return @positions;
}

sub MAIN($filename) {
    my @positions = partition($filename);

    .say for @positions;
}
