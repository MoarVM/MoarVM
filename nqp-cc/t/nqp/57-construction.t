plan(2);

class Parent {
    has $!bar;

    method bar() { $!bar }
}
class A is Parent {
    has @!foo;

    method foo() { @!foo }
}

my $x := A.bless(foo => [1, 2, 3], bar => 'BARBAR');
ok(join('|', $x.foo) eq '1|2|3', '.new() initializes child class attribute');
ok($x.bar eq 'BARBAR', '.new() initializes parent class attribute');
