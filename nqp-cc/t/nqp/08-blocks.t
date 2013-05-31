#! nqp

# check blocks and statement enders

say('1..7');

{
    say("ok 1 # blocks are okay");
}

{
    print("ok ");
    say("2 # last statement in a block does not need a semi-colon")
}


{
    say("ok 3 # statements can precede blocks");
    {
        say("ok 4 # blocks can nest");
    }
    say("ok 5 # statements can follow blocks");
}


{ print("ok ") }; { say("6 # multiple blocks on one line need a semi-colon") }

{
    print("ok ")
}; {
    say("7 # blocks following an end brace must be separated by a semicolon")
}

