#!./parrot nqp.pbc

# flattened arguments

plan(6);

sub xyz($x, $y, $z) {
    ok( $x == 7, 'first argument');
    ok( $y == 8, 'second argument');
    ok( $z == 9, 'third argument');
}

sub ijk(:$i, :$j, :$k) {
    ok( $i == 1, 'first named argument');
    ok( $j == 2, 'second named argument');
    ok( $k == 3, 'third named argument');
}

my @a := [7,8,9];
xyz(|@a);

my %a;
%a<i> := 1;
%a<j> := 2;
%a<k> := 3;
ijk(|%a);
