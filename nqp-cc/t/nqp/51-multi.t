plan(4);

proto foo($a, $b?) { * }
multi foo($a) { 1 }
multi foo($a, $b) { 2 }
ok(foo('omg') == 1);
ok(foo('omg', 'wtf') == 2);

proto bar($a?) { 'omg' ~ {*} ~ 'bbq' }
multi bar() { 'wtf' }
multi bar($a) { 'lol' }
ok(bar() eq 'omgwtfbbq');
ok(bar(42) eq 'omglolbbq');
