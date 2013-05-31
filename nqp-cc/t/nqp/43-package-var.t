#! nqp.pbc

# Accessing package variables directly

plan(4);

our $var;

$GLOBAL::var := 1;
$ABC::def := 2;
@XYZ::ghi[0] := 3;
$GLOBAL::context := 4;


ok( $var == 1, '$GLOBAL::var works');


module ABC {
    our $def;
    ok( $def == 2, '$ABC::def works');
}

module XYZ {
    our @ghi;
    ok( @ghi[0] == 3, '@XYZ::ghi works');
}

ok( $*context == 4, 'contextual in GLOBAL works');
