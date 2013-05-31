#! nqp

# check control structure 'if'

say('1..6');

if 1 { say("ok 1 # on one line"); }

say("ok 2 # statements following if are okay");

if 1 {
    say("ok 3 # multi-line if");
}

if 0 {
    print("not ");
}

say("ok 4 # multi-line if, false condition causes block not to execute");

say("ok 5 # postfix statement modifier form (true)") if 1;

print("not ") if 0;

say("ok 6 # postfix statement modifier form (false)");
