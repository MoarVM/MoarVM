plan(6);

role ParaTest[$a] {
    method m() { $a }
}

class A {
}

my $a1 := A.new();
my $a2 := A.new();
$a1.HOW.mixin($a1, ParaTest.HOW.curry(ParaTest, "foo"));
$a2.HOW.mixin($a2, ParaTest.HOW.curry(ParaTest, "bar"));
ok($a1.m eq "foo", "mixin of parametric role with first arg");
ok($a2.m eq "bar", "mixin of parametric role with second arg");

role ParaNameTest[$n, $rv] {
    method ::($n)() { $rv }
}

class B {
}

my $b := B.new();
$b.HOW.mixin($b, ParaNameTest.HOW.curry(ParaNameTest, "drink", "beer"));
$b.HOW.mixin($b, ParaNameTest.HOW.curry(ParaNameTest, "meat", "beef"));
ok($b.drink eq "beer", "parametric method name handling works (1)");
ok($b.meat eq "beef", "parametric method name handling works (2)");

grammar LolGrammar {
    token TOP { <foo> }
    proto token foo {*}
    token foo:sym<a> { <sym> }
}
role AnotherFoo[$x] {
    token foo:sym<more> { $x }
}
ok(LolGrammar.parse('abc') eq 'a', "parametric mixin/grammar/LTM interaction (sanity)");
my $derived := LolGrammar.HOW.mixin(LolGrammar, AnotherFoo.HOW.curry(AnotherFoo, 'ab'));
ok($derived.parse('abc') eq 'ab', "parametric mixin/grammar/LTM interaction");
