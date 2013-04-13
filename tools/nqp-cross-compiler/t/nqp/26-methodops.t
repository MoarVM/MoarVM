#! nqp

# method ops (just method calls for now)

plan(5);

class Foo {
  method blarg() {
    'ok 1 # method calls work';
  }
  method blargless() {
    'ok 3 # argument-less method calls work'
  }
  method blast() {
    'ok 4 # string method calls work'
  }

  method foo:bar<baz>() {
    'ok 5 # colonpair named method call work'
  }
}

class Bar {
  method blarg() {
    'not ok 1';
  }
}

sub blarg() {
  'ok 2 # regular subs aren\'t confused with methods';
}

my $foo := Foo.new();

say($foo.blarg());
say(blarg());
say($foo.blargless);
my $t := 'st';
say($foo."bla$t"());
say($foo.foo:bar<baz>());
