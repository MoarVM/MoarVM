#! nqp

# test protoregexes in grammars

plan(7);

grammar ABC {

    token TOP { <symbols> .* }

    proto token symbols { <...> }

    token symbols:sym<abc>  { <sym> }
    token symbols:sym<a>    { <sym> }
    token symbols:sym<bang> { $<sym>=['!'] }
    token symbols:sym<===>  { <sym> }
}


my $/ := ABC.parse('abcdef');
ok( ?$/ ,           'successfully matched grammar' );
ok( $/ eq 'abcdef', 'successful string match' );
ok( $<symbols> eq 'abc', 'successful protoregex match');
ok( $<symbols><sym> eq 'abc', 'correct proto candidate match' );

$/ := ABC.parse('adef');
ok( ?$/ ,           'successfully matched grammar' );

$/ := ABC.parse('xxx');
ok( !$/ ,           'successfully failed protoregex match' );

$/ := ABC.parse('xxx', :rule<symbols>);
ok( !$/ ,           'successfully failed protoregex match' );



