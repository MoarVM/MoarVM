#!./parrot nqp.pbc

# lists and for

plan(3);

my $list := (1,2,3);
my $indexer := 0;

for $list {
    print("ok "); print($_); say(" checking loop via indices");
}
