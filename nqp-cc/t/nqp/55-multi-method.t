#! nqp

plan(4);

class Foo {
    proto method bar($x?) { * }
    multi method bar() { 42 }
    multi method bar($x) { 2 * $x }
}

my $x := Foo.new();

ok($x.bar() == 42);

ok($x.bar(5) == 10);

class Baz is Foo {
    multi method bar() { 37 }
}

my $y := Baz.new();

ok($y.bar() == 37);

class Quux is Foo {
    proto method bar() { * }
    multi method bar() { 37 }
}

my $z := Quux.new();

ok($z.bar() == 37);
