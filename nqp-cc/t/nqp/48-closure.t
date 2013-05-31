#! nqp

plan(2);

sub A($a) {
    return { $a * 2 };
}

my $x := A(3);
my $y := A(5);

ok( $y() == 10, "second closure correct" );
ok( $x() == 6, "first closure correct" );

