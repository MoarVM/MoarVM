#! nqp

# check comments

say('1..8');

#Comment preceding
say("ok 1");

say("ok 2"); #Comment following

#say("not ok 3");
#          say("not ok 4");

{ say('ok 3'); } # comment
{ say('ok 4'); }

=for comment
say("not ok 5");

=for comment say("not ok 6");

=begin comment
say("not ok 7");

say("not ok 8");
=end comment

=comment say("not ok 9");
say("not ok 10");

=for comment blah

say("ok 5");

=begin comment
=end comment
say("ok 6");

# This doesn't quite work right... but it doesn't work in STD either
#=for comment
#=begin comment
#=end comment
#=say("ok 7");

=comment

say("ok 7");

    =begin comment indented pod
    this is indented pod
    say("not ok 8");
    =end comment

    say("ok 8");


