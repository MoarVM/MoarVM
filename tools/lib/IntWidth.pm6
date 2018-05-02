sub int-bitwidth (Int:D $int) is export {
    my $sign = $int < 0 ?? -1 !! 1;
    my $abs = abs($int);
    ($abs-1).base(2).chars + 1;
}
sub uint-bitwidth (Int:D $int) is export {
    die "Can't determine the bitwidth of negative integer: '$int'" if $int < 0;
    $int.base(2).chars;
}
multi sub getType (Int:D :$max, Int:D :$min) {
    die if $min.defined && $max < $min;
    my Bool $has-neg = $max < 0;
    $has-neg ||= $min < 0;
    my $bw1;
    if ($has-neg) {
        $bw1 = int-bitwidth($max);
        $bw1 = min($bw1, int-bitwidth($min)) if defined $min;
    }
    else {
        $bw1 = uint-bitwidth($max);
        $bw1 = min($bw1, uint-bitwidth($min)) if defined $min;
    }
    return getType($bw1, :isSigned($has-neg));
}
multi sub getType (UInt:D $bitwidth, Bool:D :$isSigned = True) {
    if ($bitwidth <= 8) {
        return "MVMint8";
    }
    elsif ($bitwidth <= 16) {
        return "MVMint16";
    }
    elsif ($bitwidth <= 32) {
        return "MVMint32";
    }
    elsif ($bitwidth <= 64) {
        return "MVMint64";
    }
}
getType(:min(-10), :max(0));
