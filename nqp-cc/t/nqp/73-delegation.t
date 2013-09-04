plan(10);

class Foo {
    has @!pos_foo;
    has @!pos_bar is positional_delegate;
    has @!pos_baz;

    has %!assoc_foo;
    has %!assoc_bar is associative_delegate;
    has %!assoc_baz;
    method get_pos_1() {
       @!pos_bar[1]; 
    }
    method get_assoc_1() {
       %!assoc_bar<1>; 
    }
}

my $foo := Foo.new();
$foo[0] := 123;
$foo[1] := 456;
$foo<1> := 678;
$foo<foo> := "hi";
$foo<bar> := "hello";
$foo<baz> := "world";

ok($foo[0] == 123,"getting and setting element 0");
ok($foo[1] == 456,"getting and setting element 1");
ok($foo<1> == 678,"associative access is seperate");
ok($foo<bar> eq 'hello',"assosiative storage takes strings as keys");
ok($foo.get_pos_1 == 456,"positional are stored in the attribute");
ok($foo.get_assoc_1 == 678,"associatives are stored in the attribute");

my $pos_attr;
my $assoc_attr;
for Foo.HOW.attributes(Foo) -> $attr {
  if $attr.name eq '@!pos_bar' {
    $pos_attr := $attr;
  }
  elsif $attr.name eq '%!assoc_bar' {
    $assoc_attr := $attr;
  }
}
ok($pos_attr.positional_delegate == 1,"positional_delegate is set correctly");
ok($pos_attr.associative_delegate == 0,"positional_delegate is not set incorrectly");
ok($assoc_attr.positional_delegate == 0,"associative_delegate is not set incorrectly");
ok($assoc_attr.associative_delegate == 1,"associative_delegate is set correctly");
