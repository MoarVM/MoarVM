plan(5);

my $a := nqp::getenvhash();
$a<foo> := 123;
my $b := nqp::getenvhash();
$b<foo> := 456;

ok(nqp::ishash($a),'nqp::getenvhash() returns a hash');
ok($a<foo> == 123,'nqp::getenvhash() is a fresh hash');
ok($b<foo> == 456,'nqp::getenvhash() is a fresh hash');

my $tmp_file := "tmp";
nqp::shell("echo Hello > $tmp_file",nqp::cwd(),nqp::getenvhash());
my $output := slurp($tmp_file);
ok($output ~~ /^Hello/,'nqp::shell works with the echo shell command');

my $env := nqp::getenvhash();
$env<NQP_SHELL_TEST_ENV_VAR> := "123foo";

nqp::shell("echo %NQP_SHELL_TEST_ENV_VAR% > $tmp_file",nqp::cwd(),$env);
$output := slurp($tmp_file);
if $output eq "%NQP_SHELL_TEST_ENV_VAR%\n" {
    nqp::shell("echo \$NQP_SHELL_TEST_ENV_VAR > $tmp_file",nqp::cwd(),$env);
    my $output := slurp($tmp_file);
    ok($output eq "123foo\n","passing env variables to child processes works linux");
} else {
  ok($output ~~ /^123foo/,"passing env variables to child processes works on windows");
}
