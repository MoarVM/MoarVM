#! nqp

# check module

plan(3);

XYZ::foo('ok 1');
XYZ::sayfoo();

module XYZ {
    our $value := 'ok 2';
    our sub foo($x) { $value := $x; }
    our sub sayfoo() { say($value // 'ok 1'); }
    sayfoo();
}

XYZ::foo('ok 3');
XYZ::sayfoo();

