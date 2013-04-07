#!./parrot nqp.pbc

# the ternary ?? !! operator

plan(8);

ok( 1 ?? 1 !! 0 );
ok( 0 ?? 0 !! 1 );

my $a := 1 ?? 'yes' !! 'no';
ok( $a eq 'yes' );

my $b := 0 ?? 'yes' !! 'no';
ok( $b eq 'no' );

my $c := 1 ?? 'yes' !! ( $a := 'no' );
ok( $c eq 'yes' );
ok( $a eq 'yes' );

my $d := 0 ?? ( $a := 'no' ) !! 'yes';
ok( $d eq 'yes' );
ok( $a eq 'yes' );

