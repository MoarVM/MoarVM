plan(13);

role R1 {
    has $!a;
    method role_meth() { "called role method" }
    method get_attr() { $!a }
    method set_attr($v) { $!a := $v }
    method where() { "method in " ~ $?CLASS.HOW.name($?CLASS) }
    method override_me() { "role's method - OH NO" }
}

class CL1 does R1 { }
class CL2 does R1 {
    method override_me() { "class method beat role one - YAY" }
}

ok(CL1.HOW.does(CL1, R1));
ok(CL2.HOW.does(CL2, R1));

my $x := CL1.new();
ok($x.role_meth() eq "called role method");
$x.set_attr("yay composed attrs");
ok($x.get_attr() eq "yay composed attrs");

ok(CL1.where() eq "method in CL1");
ok(CL2.where() eq "method in CL2");
ok(CL2.override_me() eq "class method beat role one - YAY");


role R3 { method a() { 1 }; method c() { 'wtf' } }
role R4 { method b() { 2 }; method c() { 'wtf' } }
class C3 does R3 does R4 { method c() { 'resolved' } }
ok(C3.a() == 1);
ok(C3.b() == 2);
ok(C3.c() eq 'resolved');

ok(!C3.HOW.does(C3, R1));
ok(C3.HOW.does(C3, R3));
ok(C3.HOW.does(C3, R4));

