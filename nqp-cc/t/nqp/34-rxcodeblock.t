#! nqp

plan(12);

grammar ABC {
    token TOP { { ok(1, 'basic code assertion'); } }
}
ABC.parse('abc');

grammar BCD {
    token TOP { $<bcd>=[.*] { ok( $<bcd> eq 'bcd', 'match in assertion' ); } }
}
BCD.parse('bcd');

grammar CDE {
    token TOP { \d+ <?{ +$/ < 255}> cde }
}
ok( ?CDE.parse('123cde'),  'passes assertion, match after');
ok( !CDE.parse('1234cde'), 'fails assertion');
ok( ?CDE.parse('0cde'),    'passes assertion, match after');
ok( !CDE.parse('1234'),    'fails assertion');
ok( !CDE.parse('123'),     'fails regex after passing assertion');

grammar DEF {
    token TOP { \d+ <!{ +$/ < 255 }> def }
}
ok( !DEF.parse('123def'),  'fails assertion');
ok( ?DEF.parse('1234def'), 'passes assertion, text after');
ok( !DEF.parse('0def'),    'fails assertion');
ok( !DEF.parse('1234'),    'passes assertion, fails text after');
ok( ?DEF.parse('999def'),  'passes assertion, text after');

