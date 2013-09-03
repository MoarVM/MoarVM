#! nqp

plan(1);

my $content := slurp("t/nqp/63-slurp.t");
ok(nqp::chars($content) == 157 || nqp::chars($content) == 165, "File slurped");

# vim: ft=perl6
