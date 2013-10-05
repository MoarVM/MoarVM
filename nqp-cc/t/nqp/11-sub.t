#!./parrot nqp.pbc

# check subs

say('1..18');

sub one ( ) {
    say("ok 1 # sub def and call");
}

one();

{
    sub two ( ) {
        say("ok 2 # sub def and call inside block");
    }
    two();
}

sub three ( ) { say("ok 3 # sub def; sub call on same line"); }; three();

sub four_five ($arg1) {
    say($arg1);
}
four_five('ok 4 # passed in 1 arg');

{
    four_five('ok 5 # calling sub in outer scope');
}

sub six () {
    "ok 6 # return string literal from sub";
}

say(six());

sub seven () {
    "ok 7 # bind sub return to variable";
}

my $retVal := seven();

unless $retVal {
    print("not ");
}
say($retVal);

sub add_stomp ($first, $last) {
    my $sum := $first + $last;
    $first  := $last - $first;
    $sum;
}

print("ok "); print(add_stomp(3,5)); say(" # returning the result of operating on arguments");

my $five  := 5;
my $six   := 6;

add_stomp($five, $six);

if $five != 5 {
    print("not ");
}
say("ok 9 # subroutines that operate on args do not affect the original arg outside the sub");

sub ten ($arg) {
    say("ok 10 # parameter with a trailing comma");
}
ten( 'dummy', );

# test that a sub can start with Q

sub Qstuff() { 11 };
say('ok ', Qstuff());

sub term:sym<self>() { 12 }
say('ok ', term:sym<self>());

say( (nqp::isinvokable(sub () {}) ?? 'ok 13' !! 'no 13' ) ~ '  nqp::isinvokable on sub');
say( (!nqp::isinvokable(666) ?? 'ok 14' !! 'no 14' ) ~ '  nqp::isinvokable on non sub');

# #73, test that a sub can start with last, next and redo
sub last_() { 15 };
say('ok ', last_());
sub next_() { 16 };
say('ok ', next_());
sub redo_() { 17 };
say('ok ', redo_());
sub eighteen($a-a) {}
say('ok paramer with a dash')
