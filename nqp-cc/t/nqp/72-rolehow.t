plan(12);
role Foo {
  method additional() {
    "hey new method!"
  }
}
role BarOverride {
  method bar() {
    200;
  }
}
class Bar {
  method bar() {
    100;
  }
}

my $methods := Foo.HOW.methods(Foo);
ok(nqp::elems($methods) == 1,'Foo has only one method');
my $method := $methods[0];
my $name := $method.name;#nqp::can($method, 'name') ?? $method.name !! nqp::getcodename($method);

my $a := Bar.new();
ok($a.bar == 100);
$a.HOW.mixin($a,BarOverride);
ok($a.HOW.name($a) eq 'Bar+{BarOverride}','the role name is part of the generated class name');

my $parents := $a.HOW.parents($a,:local(1));
ok(nqp::elems($parents) == 1,"the generated class has only one parent" );
ok($parents[0].HOW.name($parents[0] eq 'Bar'),"...and it's the correct one");

my $roles := $a.HOW.roles($a,:local(1));
ok(nqp::elems($roles) == 1,"the generated class does only one role" );
ok($roles[0].HOW.name($roles[0]) eq 'BarOverride',"...and it's the correct one");

ok($a.bar == 200);

my $b := Bar.new();
$b.HOW.mixin($b,Foo);
ok($b.bar == 100, 'we can use non overriden methods');
ok($b.additional eq 'hey new method!','new methods are inserted');

$b.HOW.mixin($b,BarOverride);
ok($b.bar == 200, 'we can apply two roles to one object');
ok($b.HOW.name($b) eq 'Bar+{Foo}+{BarOverride}','both role names are part of the generated class name');
