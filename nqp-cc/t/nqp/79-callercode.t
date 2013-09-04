plan(5);

sub foo_inner() {
  my $caller := nqp::callercode();
  $caller(7);
}
sub foo_outer($arg) {
  if $arg == 1 {
    foo_inner();
  } elsif $arg == 7 {
    ok(1,"nqp::callercode returns the correct sub");
  }
}
foo_outer(1);

sub bar($arg) {
  if $arg == 1 {
    bar(2);
  } elsif $arg == 2 {
    my $this := nqp::callercode();
    ok(nqp::getcodeobj($this) eq "first","nqp::getcodeobj works on result of nqp::callercode");
    nqp::setcodeobj($this,"second");
    $this(7);
  } elsif $arg == 7 {
    my $this := nqp::callercode();
    ok(nqp::getcodeobj($this) eq "second","nqp::setcodeobj works on result of nqp::callercode");
  }
}
nqp::setcodeobj(&bar,"first");
bar(1);

class Foo {
  has $!attr;
  method a($arg) {
    if $arg == 1 {
      $!attr := 100;
      self.b(1);
    } elsif $arg == 2 {
      ok(1,'nqp::callercode work for methods'); 
      ok($!attr == 110,'the attribute has the correct value'); 
    }
  }
  method b($arg) {
    $!attr := $!attr + 10;
    my $caller := nqp::callercode();
    $caller(self,2);
  }
}
my $foo := Foo.new();
$foo.a(1);
