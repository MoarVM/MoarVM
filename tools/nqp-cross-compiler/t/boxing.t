#!nqp
use MASTTesting;

plan(3);

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
    op(@ins, 'knowhowattr', $attr);
    op(@ins, 'const_s', $name, sval('$!foo'));
    op(@ins, 'const_i64', $bt, ival(1));
    op(@ins, 'findmeth', $meth, $attr, sval('new'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::obj, $Arg::named +| $Arg::int],
        $attr, sval('name'), $name, sval('type'), $ntype, sval('box_target'), $bt, :result($attr));
    op(@ins, 'findmeth', $meth, $how, sval('add_attribute'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj, $Arg::obj], $how, $type, $attr, :result($attr));
    
    # Compose.
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));
    
    $type
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type := boxing_type($frame, 'Int');
        my $ins := local($frame, NQPMu);
        my $str := local($frame, str);
        op(@ins, 'create', $ins, $type);
        op(@ins, 'const_s', $str, sval('alive'));
        op(@ins, 'say_s', $str);
        op(@ins, 'return');
    },
    "alive\n",
    "Can create a type with that boxes using create");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type := boxing_type($frame, 'Int');
        my $box := local($frame, NQPMu);
        my $orig_int := local($frame, int);
        my $result_int := local($frame, int);
        op(@ins, 'const_i64', $orig_int, ival(112358));
        op(@ins, 'box_i', $box, $orig_int, $type);
        op(@ins, 'unbox_i', $result_int, $box);
        op(@ins, 'say_i', $result_int);
        op(@ins, 'return');
    },
    "112358\n",
    "Can box and unbox an int");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type := boxing_type($frame, 'Num');
        my $box := local($frame, NQPMu);
        my $orig_num := local($frame, num);
        my $result_num := local($frame, num);
        op(@ins, 'const_n64', $orig_num, nval(3.14));
        op(@ins, 'box_n', $box, $orig_num, $type);
        op(@ins, 'unbox_n', $result_num, $box);
        op(@ins, 'say_n', $result_num);
        op(@ins, 'return');
    },
    "3.14\n",
    "Can box and unbox a num",
    approx => 1);
