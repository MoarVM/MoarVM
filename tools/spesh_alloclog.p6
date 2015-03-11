#!/usr/bin/env perl6

constant MEM_BLOCKSIZE = 32768;

my %graphs;
my @allocsizes;

my $abort-loop = Promise.new();

my int $linenum;

my int $destroyed;

signal(SIGINT).Promise.then({ say "aborting ..."; $abort-loop.keep });

for lines() {
    $linenum++;
    if $linenum %% 10000 {
        $linenum.say;
        if $abort-loop {
            say "aborted";
            last
        }
    }
    when / "allocd in current block " (<digit>+) " " (<xdigit>+) / {
        %graphs{$1}[*-1]<usage> += $0;
        @allocsizes.push: +$0
    }
    when / "allocd in previous block " (<digit>+) " " (<xdigit>+) / {
        %graphs{$1}[*-2]<usage> += $0;
        @allocsizes.push: +$0
    }
    when / "allocd in new block " (<digit>+) " " (<xdigit>+) / {
        %graphs{$1}.push: { usage => $0, size => MEM_BLOCKSIZE };
        @allocsizes.push: +$0
    }
    when / "created " (<xdigit>+) / {
        %graphs{$0} = [ { size => MEM_BLOCKSIZE div 4, usage => 0 } ]
    }
    when / "destroyed " (<xdigit>+) / {
        $destroyed++;
    }
    default {
        .note
    }
}

say "------------------";
say "finished ingesting";
say "------------------";

my @graphs_by_size = %graphs.pairs.sort: *.value.elems;

my $wastesum;
my $lb-wastesum;

my @waste_per_size;

for @graphs_by_size {
    my ($waste, $last-waste);
    for @(.value) {
        $waste += .<size> - .<usage>;
        $last-waste = .<size> - .<usage>
    }
    $wastesum += $waste - $last-waste;
    $lb-wastesum += $waste;
    @waste_per_size[.value.elems].push: $last-waste;
    say "graph with {.value.elems} blocks wasted {$waste - $last-waste} bytes (plus $last-waste in last block)"
}

say "";
say "all in all:";
say "$wastesum wasted at inner block-ends";
say "$lb-wastesum wasted at the very ends of graphs";

say "out of the {+%graphs} graphs, only $destroyed were destroyed ({100 * $destroyed / +%graphs}%)";

@allocsizes .= sort;

say "allocation sizes:";
say (@allocsizes R/ [+] @allocsizes), " average";
say @allocsizes[* div 2], " median";
say @allocsizes[*-1], " maximum";
say "";

say @allocsizes[* div 4], " 25th";
say @allocsizes[(* div 4) * 3], " 75th";

say "";
say "wastage at the end of the last block ...";
for @waste_per_size.pairs {
    next if .key == 0 || .values.elems == 0;
    say "for graphs with {.key} blocks ({.value.elems} in total)";
    try {
        my @allocsizes = .value.sort;
        say (@allocsizes R/ [+] @allocsizes), " average";
        printf "%10s  %10s  %10s    %10s\n",
            "25th", "50th", "75th", "maximum";
        printf "%10d  %10d  %10d    %10d\n",
            @allocsizes[* div 4],
            @allocsizes[* div 2],
            @allocsizes[(* div 4) * 3],
            @allocsizes[*-1];
    }
}
