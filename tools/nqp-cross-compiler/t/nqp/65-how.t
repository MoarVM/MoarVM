#!./parrot nqp.pbc

# check subs

plan(4);

class Foo {
    has $!foo_attr;
}
class Bar is Foo {
    has $!bar_attr;
}

my $foo := Foo.new();
my @Foo-attrs := $foo.HOW.attributes($foo,:local(1));
ok(@Foo-attrs[0].name eq '$!foo_attr',"we can get an attribute");
my $bar := Bar.new();
ok(+$bar.HOW.attributes($bar,:local(1)) == 1,"we only get local attributes");
ok(+$bar.HOW.attributes($bar,:local(0)) == 2,"we get all attributes");
ok($bar.HOW.attributes($bar) == 2,"we can skip :local");
