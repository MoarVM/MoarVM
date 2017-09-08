constant $debug = False;
use nqp;
#| Returns a multi-dim Array. We push onto each index all of the contiguous
#| codepoints which have the same bitfield row. We also fill in any gaps and
#| add those to their own range number.
sub get-points-ranges-array (%point-index, Array $sorted-points?) is export {
    my @ranges;
    my $saw = '';
    my int $i = -1;
    my int $point-no = -1;
    @ranges[0] = [];
    my @sorted-points = $sorted-points ?? @$sorted-points !! %point-index.keys.sort(*.Int);
    for @sorted-points -> $cp {
        $point-no++;
        # This code path is taken if there are noncontiguous gaps in the ranges.
        # We populate the range array's element with these missing values.
        if $cp != $point-no {
            my $between = $cp - $point-no;
            $between == 1
            ?? @ranges[++$i].push: $point-no
            !! @ranges[++$i].append: ($point-no)..($point-no + $between - 1);
            $point-no += $cp - $point-no;
            # Clear the memory
            $saw = Nil;
        }
        if $saw.defined && $saw eq nqp::atkey(%point-index, $cp) {
            @ranges[$i].push: $cp;
        }
        else {
            $saw = nqp::atkey(%point-index, $cp);
            @ranges[++$i] = [];
            @ranges[$i].push($cp);
        }
    }
    @ranges;
}
