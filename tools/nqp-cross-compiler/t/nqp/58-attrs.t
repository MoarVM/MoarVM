#! nqp

plan(2);

class Foo {
    has $!answer;
    method question($what) { $!answer := $what }
    method answer() { $!answer }
};

my $first  := Foo.new;
my $second := Foo.new;

$first.question(42);
$second.question(23);
ok($first.answer  == 42, "attributes work");
ok($second.answer == 23, "... and are not shared among objects");

