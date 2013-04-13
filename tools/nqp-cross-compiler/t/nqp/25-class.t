#!./parrot nqp.pbc

# class

plan(2);

class XYZ {
    method foo($x) {
        say($x);
    }
}

my $xyz := XYZ.new();

$xyz.foo('ok 1');


# test that a class can start with Q

class QRS {
    method foo($x) {
        say($x);
    }
}

my $qrs := QRS.new();

$qrs.foo('ok 2');

