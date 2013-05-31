knowhow Foo {
    has $!bbq;
    method new() { nqp::create(self) }
    method lol() {
        "yay, methods"
    }
    method set_bbq($bbq) { $!bbq := $bbq; }
    method get_bbq() { $!bbq }
}

plan(3);

ok(Foo.lol eq "yay, methods", "method calls on knowhow type object");

my $x := Foo.new;
ok($x.lol eq "yay, methods", "method calls on knowhow instance");

$x.set_bbq("wurst");
ok($x.get_bbq eq "wurst", "attributes on knowhow instance");
