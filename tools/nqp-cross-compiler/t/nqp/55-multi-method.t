#! nqp

plan(4);

class Foo {
    proto method bar($x?) { * }
    multi method bar() { 42 }
    multi method bar($x) { 2 * $x }
}

my $x := Foo.new();

if $x.bar() == 42 {
    say("ok 1");
}

if $x.bar(5) == 10 {
    say("ok 2");
}

class Baz is Foo {
    multi method bar() { 37 }
}

my $y := Baz.new();

if $y.bar() == 37 {
    say("ok 3");
}

class Quux is Foo {
    proto method bar() { * }
    multi method bar() { 37 }
}

my $z := Quux.new();

if $z.bar() == 37 {
    say("ok 4");
}
