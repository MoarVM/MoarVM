plan(3);
my $outer := "hello there";
sub foo() {
  my $foo := "hello";
  my $bar := "hi";
  my $lexpad := nqp::curlexpad();
  ok(nqp::atkey($lexpad,'$foo') eq "hello","accessing a variable using nqp::curlexpad()");
  ok(nqp::atkey($lexpad,'$bar') eq "hi","accessing a different variable using nqp::curlexpad()");
  $lexpad;
}
my $pad := foo();
ok(nqp::atkey($pad,'$bar') eq "hi","accessing a variable using nqp::curlexpad outside of that sub");


