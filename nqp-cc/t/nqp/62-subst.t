#! nqp

plan(7);

my $str := 'hello';

ok(subst($str, /h/, 'f') eq 'fello', 'We can use subst');
ok($str                  eq 'hello', '.. withouth side effect');

ok(subst($str, /l/, 'r', :global) eq 'herro', 'We can use subst to replace all matches');

my $i := 0;
ok(subst($str, /l/, -> $m {$i++}) eq 'he0lo', 'We can have a closure as replacement');
ok($i == 1, '.. and $i updated');

ok(subst($str, /FOO/, 'BAR') eq 'hello', "Non-existing string doesn't clobber string");
ok(subst($str, /FOO/, 'BAR', :global) eq 'hello', "Non-existing string doesn't clobber string globally");

# vim: ft=perl6

