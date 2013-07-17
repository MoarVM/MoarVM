plan(3);
class Foo {
    has $!counter;
    has $!bool;
    method BUILD() {
      $!bool := 1;
    }
    method half-true() {
        $!bool := !$!bool;
        $!bool;
    }
}
my $table := Foo.HOW.method_table(Foo);
my $method := $table{'half-true'};
my $foo := Foo.new();
nqp::setboolspec(Foo,0,$method);
ok(nqp::istrue($foo) == 0);
ok(nqp::istrue($foo) == 1);
ok(nqp::istrue($foo) == 0);

