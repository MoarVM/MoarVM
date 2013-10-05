plan(7);

role TheRole {
    has $!a;
    method role_meth() { "called role method" }
    method get_attr() { $!a }
    method set_attr($v) { $!a := $v }
    method override_me() { "role method" }
}

class Example {
    method override_me() { "class method" }
}

my $obj := Example.new();
my $obj_m := Example.new();

ok($obj.override_me() eq "class method", "sanity (1)");
ok($obj_m.override_me() eq "class method", "sanity (2)");

$obj_m.HOW.mixin($obj_m, TheRole);

ok(nqp::istype($obj_m, TheRole),"after mixing in a role the object is still the old type");

ok($obj_m.role_meth() eq "called role method", "role method mixed in");

$obj_m.set_attr("stout");
ok($obj_m.get_attr() eq "stout", "attributes from role work properly");

ok($obj_m.override_me() eq "role method", "mixed in method overrides original one");
ok($obj.override_me() eq "class method", "mixing in is per object");
