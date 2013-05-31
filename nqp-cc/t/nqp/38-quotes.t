#! nqp

# interpolating quotes

plan(7);

my $abc := 'abc';

ok( "xxx$abc" eq 'xxxabc', "basic scalar interpolation" );

ok( qq{xxx $abc zzz} eq 'xxx abc zzz', 'basic qq{} interpolation' );

my $num := 5;

ok( "xxx {3+$num} zzz" eq 'xxx 8 zzz', "basic closure interpolation" );

ok( qq{xxx {3+$num} zzz} eq 'xxx 8 zzz', 'basic qq{} closure interpolation' );

ok( < a > eq 'a', 'spaces around individual element stripped');

ok( +< a b > == 2, 'angle quotes correctly produce list');

ok( nqp::islist(< >), 'empty angle quotes correctly produce list');
