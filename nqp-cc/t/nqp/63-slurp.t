#! nqp

plan(1);

my $content := slurp("t/nqp/63-slurp.t");
ok($content, "File slurped");

# vim: ft=perl6
