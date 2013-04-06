#! nqp

# check comparisons

say('1..19');

##Integers, positive and negative

if 1 == 1 { say("ok 1 # numeric equality, integers"); }

unless 1 == 2 {
    say("ok 2 # numeric equality, integers, not equal");
}

if -3 == -3 { say("ok 3 # numeric equality, negative integers"); }

if 1 != 2 { say("ok 4 # numeric inequality, integers"); }

unless 1 != 1 {
    say("ok 5 # numeric inequality, equal, integers");
}

unless -2 != -2 {
    say("ok 6 # numeric inequality, equal, negative integers");
}

##Strings

if "eq" eq "eq" { say("ok 7 # string equality"); }

unless "one" eq "two" {
    say("ok 8 # string equality, not equal");
}

if "ONE" ne "TWO" { say("ok 9 # string inequality"); }

unless "STRING" ne "STRING" {
    say("ok 10 # string inequality, equal");
}

##Coerce strings into integers

if "11" ne ~11 {
    print("not ");
}
say("ok 11 # coerce integer 11 into string eleven");

if "-12" ne ~-12 {
    print("not ");
}
say("ok 12 # coerce integer -12 into string twelve");

##Coerce integers into strings

if 13 ne +"13" {
    print("not ");
}
say("ok 13 # coerce string 13 into an integer");

if -14 ne +"-14" {
    print("not ");
}
say("ok 14 # coerce string -14 into an integer");

##Containers

if nqp::eqaddr((1,2),(1,2)) {
    print("not ");
}
say("ok 15 # container equality, unnamed arrays");

my @a := (1, 2);

unless nqp::eqaddr(@a,@a) {
    print("not ");
}
say("ok 16 # container equality, self");

my @b := @a;

unless nqp::eqaddr(@a,@b) {
    print("not ");
}
say("ok 17 # container equality, named arrays");

my $x := 'foo';
my $y := $x;
my $z := 'bar';

unless nqp::eqaddr($x,$y) {
    print("not ");
}
say("ok 18 # container equality, string binding");

if nqp::eqaddr($x,$z) {
    print("not ");
}
say("ok 19 # container equality, string value");
