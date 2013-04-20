#! nqp

# check '||', '&&', and '//'

plan(7);

my $or_no_branch := 1;
$or_no_branch || ( $or_no_branch := 0 );
ok($or_no_branch, "logical || shortcuts, branch not taken");

my $or_branch := 0;
0 || ( $or_branch := 1 );
ok($or_branch, "logical || shortcuts, branch taken");

my $and_no_branch := 0;
$and_no_branch && ( $and_no_branch := 1 );
ok(!$and_no_branch, "logical && shortcuts, branch not taken");

my $and_branch := 0;
1 && ( $and_branch := 1 );
ok($and_branch, "logicl && shortcuts, branch taken");

my $err_no_branch := 1;
$err_no_branch // ( $err_no_branch := -1 );
ok($err_no_branch == 1, "logical // shortcuts on true, branch not taken");

$err_no_branch := 0;
$err_no_branch // ( $err_no_branch := -1 );
ok($err_no_branch == 0, "logical // shortcuts on defined false, branch not taken");

my $err_branch;
$err_branch // ( $err_branch := 1 );
ok($err_branch == 1, "logical // takes branch on undef");
