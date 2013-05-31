#!./parrot nqp.pbc

# slurpy args

plan(6);

sub slurpy_pos(*@pos) {
    for @pos {
        say("ok " ~ $_);
    }
}

slurpy_pos(1, 2, 3);

sub slurpy_named(*%named) {
    say(%named<pivo>);
    say(%named<slanina>);
}

slurpy_named(:pivo("ok 4"), :slanina("ok 5"));

sub named_and_slurpy(:$x, *@foo) {
    say($x);
};

named_and_slurpy(1, :x('ok 6'));
