plan(10);
my $match := 'abcdef' ~~ / c(.)<alpha> /;
ok( $match eq 'cde', "simple match" );
for $match.list {
  ok($_ eq 'd','correct numbered capture');
}
for $match.hash {
  ok($_.key eq 'alpha','the named capture is named correctly');
  ok($_.value eq 'e','...and it contains the right things');
}
ok( $match.from == 2, ".from works" );
ok( $match.to == 5, ".to works");
ok( $match.orig eq "abcdef", ".orig works");
ok( $match.chars == 3, ".chars works");


ok($match."!dump_str"('mob') eq "mob: cde @ 2\nmob[0]: d @ 3\nmob<alpha>: e @ 4\n",".\"!dump_str\" works correctly");

grammar ABC {
    token TOP { (o)(k) ' ' <integer> }
    token integer { \d+ }
}

$match := ABC.parse('ok 123');
ok($match.dump eq "- 0: o\n- 1: k\n- integer: 123\n",".dump works correctly");
