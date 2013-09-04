#! nqp

plan(37);

my $b := "b+";
my @foo := [ "b+", "c+" ];

ok("ab+d" ~~ /a $b d/,     'plain scalar interpolates as literal 1');
ok(!("abbbbbd" ~~ /a $b d/), 'plain scalar interpolates as literal 2');

ok("ab+d" ~~ /a @foo d/,     'plain array interpolates as alternations of literals 1');
ok("ac+d" ~~ /a @foo d/,     'plain array interpolates as alternations of literals 2');
ok(!("abbbbbd" ~~ /a @foo d/), 'plain array interpolates as alternations of literals 3');
ok(!("acccccd" ~~ /a @foo d/), 'plain array interpolates as alternations of literals 4');

my @ltm := [ "b", "bb", "bbc", "bc" ];

ok(("abd" ~~ / @ltm /) eq 'b', 'array finds longest match 1');
ok(("abbd" ~~ / @ltm /) eq 'bb', 'array finds longest match 2');
ok(("abbcd" ~~ / @ltm /) eq 'bbc', 'array finds longest match 3');
ok(("abccd" ~~ / @ltm /) eq 'bc', 'array finds longest match 4');

ok(("abd" ~~ / || @ltm /) eq 'b', '|| array hits first match 1');
ok(("abbd" ~~ / || @ltm /) eq 'b', '|| array hits first match 2');
ok(("abbcd" ~~ / || @ltm /) eq 'b', '|| array hits first match 3');
ok(("abccd" ~~ / || @ltm /) eq 'b', '|| array hits first match 4');

ok(!("ab+d"  ~~ /a <$b> d/), 'scalar assertion interpolates as regex 1');
ok("abbbbbd" ~~ /a <$b> d/, 'scalar assertion interpolates as regex 2');

ok(!("ab+d" ~~ /a <@foo> d/),   'array assertion interpolates as alternations of regexen 1');
ok(!("ac+d" ~~ /a <@foo> d/),   'array assertion interpolates as alternations of regexen 2');
ok("abbbbbd" ~~ /a <@foo> d/, 'array assertion interpolates as alternations of regexen 3');
ok("acccccd" ~~ /a <@foo> d/, 'array assertion interpolates as alternations of regexen 4');

ok(!("ab+d" ~~ /a <{ "b+" }> d/), 'code assersion interpolates as regex 1');
ok("abbbbd" ~~ /a <{ "b+" }> d/, 'code assersion interpolates as regex 2');

ok("abbbbd" ~~ /a <{ ["b+", "c+"] }> d/, 'code assertion that returns array interpolates as alternations of regexen 1');
ok("accccd" ~~ /a <{ ["b+", "c+"] }> d/, 'code assertion that returns array interpolates as alternations of regexen 2');

my $r := /b+/;

ok(!("ab+d" ~~ /a $r d/), 'plain scalar containing precompiled regex 1');
ok("abbbd" ~~ /a $r d/, 'plain scalar containing precompiled regex 2');

my @r := [ /b+/, "c+" ];

ok("abbbbd" ~~ /a @r d/, 'plain array containing mix of precompiled and literal 1');
ok("ac+d" ~~ /a @r d/, 'plain array containing mix of precompiled and literal 1');

my $xyz := 'xyz';

ok("axyzxyzd" ~~ /a $xyz+ d/, 'Quantified plain scalar 1');
ok("ab+b+b+d" ~~ /a $b+ d/, 'Quantified plain scalar 2');
ok("abbbc+bbbd" ~~ /a @r+ d/, 'Quantified plain array');
ok("abbbcccbbcd" ~~ /a <{ [ "b+", /c+/ ] }>+ d/, 'Quantified code assertion');

ok("ad" ~~ /a { "bc" } d/, "Plain closure doesn't interpolate 1");
ok(!("abcd" ~~ /a { "bc" } d/), "Plain closure doesn't interpolate 2");

ok("ad" ~~ /a <?{ 1 }> d/, 'Zero-width assertions still work 1');
ok(!("ad" ~~ /a <!{ 1 }> d/), 'Zero-width assertions still work 2');

ok("test.h" ~~ /.h$/, 'Do not parse $/ as variable interpolation');

