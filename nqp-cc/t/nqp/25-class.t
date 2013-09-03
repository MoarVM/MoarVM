#!./parrot nqp.pbc

# class

plan(10);

class XYZ {
    method foo($x) {
        ok($x eq 'ok 1');
    }
}

my $xyz := XYZ.new();

$xyz.foo('ok 1');


# test that a class can start with Q

class QRS {
    method foo($x) {
        ok($x eq 'ok 2');
    }
}

my $qrs := QRS.new();

$qrs.foo('ok 2');

class ABC {
    my $foo := 15;
    my sub helper($x) {
        $x*2;
    }
    method foo($x) {
        helper($x);
    }
    method bar($x) {
      $foo := $foo + $x;
      $foo;
    }
    method baz($a,$b,$c,:$j,:$k) {
      ok($a == 10);
      ok($b == 11);
      ok($c == 12);
      ok($j == 13);
      ok($k == 14);
    }
}

my $abc := ABC.new();
ok($abc.foo(100) == 200,"using a lexical sub inside a method");
ok($abc.bar(10) == 25,"using a outer lexical inside a method");
ok($abc.bar(1) == 26,"the value of the lexical persisting");

my @args := [11,12];
my %args;
%args<k> := 14;
$abc.baz(10,|@args,:j(13),|%args);
