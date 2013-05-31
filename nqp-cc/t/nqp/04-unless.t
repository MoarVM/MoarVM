#! nqp

# check control structure 'unless'

say('1..6');

unless 0 { say("ok 1 # on one line"); }

say("ok 2 # statements following unless are okay");

unless 0 {
    say("ok 3 # multi-line unless");
}

unless 1 {
    print("not ");
}
say("ok 4 # testing conditional");

say("ok 5 # postfix statement modifier form (false)") unless 0;

print("not ") unless 1;

say("ok 6 # postfix statement modifier form (true)");

