#! nqp

plan(3);

my $runs := 0;

while $runs < 5 {
    $runs++;
    last if $runs == 3;
}

ok($runs == 3, "last works in while");

$runs := 0;
my $i := 0;
while $runs < 5 {
    $runs++;
    next if $runs % 2;
    $i++;
}

ok($i == 2, "next works in while");

$runs := 0;
$i := 0;
while $i < 5 {
    $runs++;
    redo if $runs % 2;
    $i++;
}

ok($runs == 10, "redo works in while");
