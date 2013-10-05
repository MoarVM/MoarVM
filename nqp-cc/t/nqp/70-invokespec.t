plan(2);
class Foo {
    has $!here_we_keep_the_code_ref;
    has $!other_place_we_could_keep_the_code_ref_in;
    method set_code_ref($code_ref) {
        $!here_we_keep_the_code_ref := $code_ref;
    }
    method set_code_ref_differently($code_ref) {
        $!other_place_we_could_keep_the_code_ref_in := $code_ref;
    }
}
class Bar is Foo {
}
nqp::setinvokespec(Foo,Foo,'$!here_we_keep_the_code_ref',nqp::null());

nqp::setinvokespec(Bar,Foo,'$!other_place_we_could_keep_the_code_ref_in',nqp::null());

my $foo := Foo.new();
$foo.set_code_ref(sub () {123});
$foo.set_code_ref_differently(sub () {456});
ok($foo() == 123,"basic setinvokespec");

my $bar := Bar.new();
$bar.set_code_ref(sub () {1001});
$bar.set_code_ref_differently(sub () {1002});
ok($bar() == 1002,"setinvokespec with a attribute in a subclass");

