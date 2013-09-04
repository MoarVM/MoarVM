#!./parrot nqp.pbc

plan(5);

class Foo {
    has $!abc;

    method foo() { $!abc := 1 };

    method uno() {
        self.foo();
    };

    method des() {
        if 1 {
            self.foo();
        }
    };

    method tres($a) {
        if 1 {
            self.foo();
        }
    };

    method quat() {
        for 2,3 -> $a { 
            ok($a + $!abc, 'Can access attribute within lexical block');
        }
    }
};

ok(Foo.new.uno, "Can access self within method");
ok(Foo.new.des, "Can access self within sub-block");
ok(Foo.new.tres(42), "Can access self within method with signature");

Foo.new.quat;
