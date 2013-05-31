#! nqp

# Test grammars and regexes

plan(6);

grammar ABC {
    token TOP { ok ' ' <integer> }
    token integer { \d+ }
    token TOP2 { ok ' ' <int-num> }
    token int-num { \d+ }
}

my $match := ABC.parse('not ok');
ok( !$match, 'parse method works on negative match');

ok( $match.chars == 0, 'failed match has 0 .chars');

$match := ABC.parse('ok 123');
ok( ?$match, 'parse method works on positive match');

ok( $match<integer> == 123, 'captured $<integer>');

$match := ABC.parse('ok 123', :rule<TOP2> );
ok( ?$match, 'parse method works with :rule');

ok( $match<int-num> == 123, 'captured $<int-num>');
