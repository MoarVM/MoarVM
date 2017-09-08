constant $bs = 8;
constant $debug = False;
#| Gets the remaining number of items left to pack
sub num-remain (%h) {
    [+] %h.values.».elems;
}
sub exists-and-elems ( %h, $key ) {
    %h{$key}:exists and %h{$key}.elems;
}
sub compute-packing ( @list where { .all ~~ Pair and $_.».value.all ~~ Int } ) is export {
    say "Received list: ", @list.perl if $debug;
    my @result;
    my $i = 0;
    my $visual;
    my %h{Int} = first-run(@list, @result);
    say %h.perl if $debug;
    say "before second run Packing:", @result.perl if $debug;

    second-run(%h, @result);
    say "before loopy Packing:", @result.perl if $debug;
    loopy(%h, @result);
    say "after loopy" if $debug;
    try say "packing:", @result.».value if $debug;
    push-remaining(%h, @result);
    die unless num-remain(%h) == 0;
    say "Final packing:", @result.».value if $debug;

    @result;
}
sub test-it {
    my @list = init();
    say compute-packing(@list);
}
sub push-remaining (%h, @result) {
    for %h.keys.sort(-*) -> $key {
        while exists-and-elems(%h, $key) {
            @result.push(%h{$key}.pop => $key);
        }
    }
}
sub loopy (%h, @result) {
    my $left;
    my $i;
    repeat while $left != [+] %h.values.».elems {
        $left = [+] %h.values.».elems;
        say "left: ", $left if $debug;
        say "First final-run" if $debug;
        final-run(%h, @result);
        say "Remaining: ", %h.perl if $debug;
        say "Packing:", @result.perl if $debug;
        $i++;
        say "thing", "$left {[+] %h.values.».elems}" if $debug;
        last if $left == 0;
    }
}
sub init {
    my $i = 0;
    my @list;
    for ^10 {
        @list.push($i++ => (1..8).pick) for ^5;
    }
    @list.push($i++ => 1) for ^10;
    @list.push($i++ => 15);
    @list;
}
sub first-run (@list, @result) {
    # First categorize everything divisible by the bs
    sub mapper(Pair $i) returns List {
        $i.value %% $bs ?? 'div' !! 'not-div',
    }
    my $a = categorize &mapper, @list;
    say "first run \$a: ", $a.perl if $debug;
    unless $a<div>:!exists {
        for $a<div>.flat {
            @result.push($_);
        }
    }
    my %h{Int};
    unless $a<not-div>:!exists {
        my $b = $a<not-div>;
        # Make a hash whose keys are the bitwidth and hold an array of which items they
        # represent
        for $b.flat -> $pair {
            my $value = $pair.value;
            my $item = $pair.key;
            push %h{$value}, $item  ;
        }
    }
    %h;
}
sub get-remain ($piece) {
    abs(abs($bs - $piece) - ($piece.Int div $bs) * $bs);
}
sub second-run (%h, @result) {
    # Start with the largest items
    for %h.keys.sort(-*) -> $key {
        # Find ones which are complement 6 and 2 for example, 3 and 3
        my $a2 = get-remain($key);
        say "1 key $key a2 $a2" if $debug;
        while (%h{$key}:exists and %h{$key}.elems) and (%h{$a2}:exists and %h{$a2}.elems) {
            # For the case of 4 and 4, make sure we have at least 2 elems
            last if $key == $a2 and %h{$key}.elems < 2;
            my $p1 = %h{$key}.pop => $key;
            my $p2 = %h{$a2}.pop => $a2;
            @result.push($p1);
            @result.push($p2);
        }
    }
}

sub final-run (%h, @result) {
    my $tot = 0;
    for %h.keys.sort(-*) -> $key {
        next unless %h{$key}:exists and %h{$key}.elems;
        my $temp_tot = $tot + $key;
        my $pushed-yet = False;
        for %h.keys.sort(-*) -> $key2 {
            next unless %h{$key2}:exists and %h{$key2}.elems;
            while $temp_tot + $key2 <= 8 {
                last if $key == $key2 and %h{$key}.elems < 2;
                last if %h{$key2}.elems < 1;
                say "key[$key] key2 [$key2] temp_tot[$temp_tot] tot[$tot] tot + key2[{$tot + $key2}]" if $debug;
                unless $pushed-yet {
                    @result.push((%h{$key}.pop orelse die $_) => $key);
                    say "Pushing key $key" if $debug;
                    $pushed-yet = True
                }
                @result.push((%h{$key2}.pop orelse die $_) => $key2);
                say "Pushing key2 $key2" if $debug;
                $temp_tot += $key2;
                $tot = $temp_tot;
            }

        }

    }
}
