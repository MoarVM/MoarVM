#!./parrot nqp.pbc

# Testing boolean context operators, ! and ?

plan(8);

##Negation
ok(!0,   'prefix negation on integer 0');
ok(!"0", 'prefix negation on string 0');

if !1 {
    print("not");
}
ok(1, "negating integer 1");

ok(!!1, 'double negation on 1');

##Boolean context
ok(?1,    'prefix negation on integer 1');
ok(?"10", 'prefix negation on string 10');

if ?0 {
    print("not");
}
ok(1, "boolean integer 0");

ok(!?!?1, 'spaghetti chaining');
