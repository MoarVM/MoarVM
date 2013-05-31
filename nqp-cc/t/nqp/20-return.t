#! nqp

# implicit and explicit returns from subs

plan(3);

sub foo() { 1; }


sub bar() {
    return 2;
    0;
}

sub baz() {
    if (1) { return 3; }
    0;
}

ok( foo() == 1 , 'last value in block' );
ok( bar() == 2 , 'explicit return value in block');
ok( baz() == 3 , 'explicit return from nested block');

