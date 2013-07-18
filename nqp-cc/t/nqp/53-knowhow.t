knowhow Foo {
    has $!bbq;
    method new() { nqp::create(self) }
    method lol() {
        "yay, methods"
    }
    method set_bbq($bbq) { $!bbq := $bbq; }
    method get_bbq() { $!bbq }
}

plan(12);

ok(Foo.lol eq "yay, methods", "method calls on knowhow type object");

my $x := Foo.new;
ok($x.lol eq "yay, methods", "method calls on knowhow instance");

$x.set_bbq("wurst");
ok($x.get_bbq eq "wurst", "attributes on knowhow instance");
ok(Foo.HOW.name(Foo), "getting the name using the HOW works correctly");

my $attrs := Foo.HOW.attributes(Foo);
ok($attrs[0].name eq '$!bbq',"we can get the attributes");

my $foo_attr := nqp::knowhowattr().new(:name('$!foo'));
ok($foo_attr.name eq '$!foo',"created attribute has correct name");

knowhow Bar {
    has $!foo;
    has $!bar;
    method argh() {
    }
}

my $methods := Foo.HOW.methods(Foo);
ok(nqp::existskey($methods,'lol'),'lol method exists in Foo.HOW.methods');
ok(nqp::existskey($methods,'new'),'new method exists in Foo.HOW.methods');
ok(nqp::existskey($methods,'set_bbq'),'set_bbq method exists in Foo.HOW.methods');
ok(nqp::existskey($methods,'get_bbq'),'get_bbq method exists in Foo.HOW.methods');
ok(!nqp::existskey($methods,'argh'),"argh doesn't exist in Foo.HOW.methods");
ok(nqp::existskey(Bar.HOW.methods(Bar),'argh'),'different knowhows have seperate method sets');
