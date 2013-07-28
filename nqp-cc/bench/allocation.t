#!nqp
use MASTTesting;

plan(1);

sub simple_type_from_repr($frame, $name_str, $repr_str) {
    my @ins := $frame.instructions;
    my $name := local($frame, str);
    my $repr := local($frame, str);
    my $how  := local($frame, NQPMu);
    my $type := local($frame, NQPMu);
    my $meth := local($frame, NQPMu);

    # Create the type.
    op(@ins, 'const_s', $name, sval($name_str));
    op(@ins, 'const_s', $repr, sval($repr_str));
    op(@ins, 'knowhow', $how);
    op(@ins, 'findmeth', $meth, $how, sval('new_type'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::str],
        $how, sval('name'), $name, sval('repr'), $repr, :result($type));

    # Compose.
    op(@ins, 'gethow', $how, $type);
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));

    $type
}

# Called with Int/Num/Str. Makes a P6opaque-based type that boxes the
# appropriate native.
sub boxing_type($frame, $name_str) {
    # Create the unboxed type.
    my $ntype := simple_type_from_repr($frame, nqp::lc($name_str), 'P6' ~ nqp::lc($name_str));

    my @ins := $frame.instructions;
    my $name := local($frame, str);
    my $bt   := local($frame, int);
    my $how  := local($frame, NQPMu);
    my $type := local($frame, NQPMu);
    my $meth := local($frame, NQPMu);
    my $attr := local($frame, NQPMu);

    # Create the type.
    op(@ins, 'const_s', $name, sval($name_str));
    op(@ins, 'knowhow', $how);
    op(@ins, 'findmeth', $meth, $how, sval('new_type'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str], $how, sval('name'), $name, :result($type));

    # Add an attribute.
    op(@ins, 'gethow', $how, $type);
#    op(@ins, 'knowhowattr', $attr);
#    op(@ins, 'const_s', $name, sval('$!foo'));
#    op(@ins, 'const_i64', $bt, ival(1));
#    op(@ins, 'findmeth', $meth, $attr, sval('new'));
#    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::obj, $Arg::named +| $Arg::int],
#        $attr, sval('name'), $name, sval('type'), $ntype, sval('box_target'), $bt, :result($attr));
#    op(@ins, 'findmeth', $meth, $how, sval('add_attribute'));
#    call(@ins, $meth, [$Arg::obj, $Arg::obj, $Arg::obj], $how, $type, $attr, :result($attr));

    # Compose.
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));

    $type
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := boxing_type($frame, 'Int');
        my $r3 := local($frame, NQPMu);
        op(@ins, 'const_i64', $r0, ival(100000000));
        my $loop := label('loop');
        nqp::push(@ins, $loop);
        op(@ins, 'create', $r3, $r1);
        op(@ins, 'dec_i', $r0);
        op(@ins, 'if_i', $r0, $loop);
        op(@ins, 'return');
    },
    "",
    "creating and collecting 100 million objects", 1);
