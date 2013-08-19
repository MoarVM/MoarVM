plan(3);

sub foo($arg) {
  my $this := nqp::curcode();
  if $arg == 1 {
    $this(7);
  } elsif $arg == 7 {
    ok("nqp::curcode returns the correct sub");
  }
}
foo(1);

sub bar($arg) {
  my $this := nqp::curcode();
  if $arg == 1 {
    ok(nqp::getcodeobj($this) eq "first","nqp::getcodeobj works on result of nqp::curcode");
    nqp::setcodeobj($this,"second");
    $this(7);
  } elsif $arg == 7 {
    ok(nqp::getcodeobj($this) eq "second","nqp::setcodeobj works on result of nqp::curcode");
  }
}
nqp::setcodeobj(&bar,"first");
bar(1);
