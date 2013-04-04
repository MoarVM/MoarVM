#!./parrot nqp.pbc

# postcircumfix:<( )>

plan(1);

my $sub := { ok(1, 'works'); }
$sub();
