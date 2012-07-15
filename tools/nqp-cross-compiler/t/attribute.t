#!nqp
use MASTTesting;

plan(1);

sub obj_attr_type($frame) {
    my @ins := $frame.instructions;
    my $name := local($frame, str);
    my $how  := local($frame, NQPMu);
    my $type := local($frame, NQPMu);
    my $meth := local($frame, NQPMu);
    my $attr := local($frame, NQPMu);
    
    # Create the type.
    op(@ins, 'const_s', $name, sval('ObjAttrType'));
    op(@ins, 'knowhow', $how);
    op(@ins, 'findmeth', $meth, $how, sval('new_type'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str], $how, sval('name'), $name, :result($type));
    
    # Add an attribute.
    op(@ins, 'gethow', $how, $type);
    op(@ins, 'knowhowattr', $attr);
    op(@ins, 'const_s', $name, sval('$!foo'));
    op(@ins, 'findmeth', $meth, $attr, sval('new'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::obj],
        $attr, sval('name'), $name, sval('type'), $attr, :result($attr));
    op(@ins, 'findmeth', $meth, $how, sval('add_attribute'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj, $Arg::obj], $type, $how, $attr, :result($attr));
    
    # Compose.
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));
    
    $type
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type := obj_attr_type($frame);
        my $ins := local($frame, NQPMu);
        my $str := local($frame, str);
        op(@ins, 'create', $ins, $type);
        op(@ins, 'const_s', $str, sval('alive'));
        op(@ins, 'say_s', $str);
        op(@ins, 'return');
    },
    "alive\n",
    "Can create a type with an attribute");
