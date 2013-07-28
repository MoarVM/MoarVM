#!nqp
use MASTTesting;

plan(5);

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

sub obj_with_attr($frame, $attr_type) {
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
        $attr, sval('name'), $name, sval('type'), $attr_type, :result($attr));
    op(@ins, 'findmeth', $meth, $how, sval('add_attribute'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj, $Arg::obj], $how, $type, $attr, :result($attr));

    # Compose.
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));

    $type
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $attr_type := local($frame, NQPMu);
        op(@ins, 'knowhow', $attr_type);
        my $type := obj_with_attr($frame, $attr_type);
        my $ins := local($frame, NQPMu);
        my $str := local($frame, str);
        op(@ins, 'create', $ins, $type);
        op(@ins, 'const_s', $str, sval('alive'));
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "alive\n",
    "Can create a type with an attribute");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $attr_type := local($frame, NQPMu);
        op(@ins, 'knowhow', $attr_type);
        my $type := obj_with_attr($frame, $attr_type);
        my $ins := local($frame, NQPMu);
        my $name := local($frame, str);
        my $exp := local($frame, NQPMu);
        my $got := local($frame, NQPMu);
        my $res := local($frame, int);
        my $str := local($frame, str);

        # Create an instance.
        op(@ins, 'create', $ins, $type);

        # Create another instance to serve as a test object and store it.
        op(@ins, 'create', $exp, $type);
        op(@ins, 'const_s', $name, sval('$!foo'));
        op(@ins, 'bindattrs_o', $ins, $type, $name, $exp);

        # Look it up again and compare it.
        op(@ins, 'getattrs_o', $got, $ins, $type, $name);
        op(@ins, 'eqaddr', $res, $got, $exp);
        op(@ins, 'coerce_is', $str, $res);
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "1\n",
    "Can store and look up an object attribute");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $attr_type := simple_type_from_repr($frame, 'int', 'P6int');
        my $type := obj_with_attr($frame, $attr_type);
        my $ins := local($frame, NQPMu);
        my $name := local($frame, str);
        my $exp := local($frame, int);
        my $got := local($frame, int);
        my $str := local($frame, str);

        # Create an instance.
        op(@ins, 'create', $ins, $type);

        # Set value.
        op(@ins, 'const_i64', $exp, ival(987));
        op(@ins, 'const_s', $name, sval('$!foo'));
        op(@ins, 'bindattrs_i', $ins, $type, $name, $exp);

        # Look it up again and output it.
        op(@ins, 'getattrs_i', $got, $ins, $type, $name);
        op(@ins, 'coerce_is', $str, $got);
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "987\n",
    "Can store and look up an integer attribute");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $attr_type := simple_type_from_repr($frame, 'num', 'P6num');
        my $type := obj_with_attr($frame, $attr_type);
        my $ins := local($frame, NQPMu);
        my $name := local($frame, str);
        my $exp := local($frame, num);
        my $got := local($frame, num);
        my $str := local($frame, str);

        # Create an instance.
        op(@ins, 'create', $ins, $type);

        # Set value.
        op(@ins, 'const_n64', $exp, nval(46.7));
        op(@ins, 'const_s', $name, sval('$!foo'));
        op(@ins, 'bindattrs_n', $ins, $type, $name, $exp);

        # Look it up again and output it.
        op(@ins, 'getattrs_n', $got, $ins, $type, $name);
        op(@ins, 'coerce_ns', $str, $got);
        op(@ins, 'say', $str);
        op(@ins, 'return');
    },
    "46.7\n", approx => 1,
    "Can store and look up a float attribute");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $attr_type := simple_type_from_repr($frame, 'str', 'P6str');
        my $type := obj_with_attr($frame, $attr_type);
        my $ins := local($frame, NQPMu);
        my $name := local($frame, str);
        my $exp := local($frame, str);
        my $got := local($frame, str);

        # Create an instance.
        op(@ins, 'create', $ins, $type);

        # Set value.
        op(@ins, 'const_s', $exp, sval("omg a kangaroo"));
        op(@ins, 'const_s', $name, sval('$!foo'));
        op(@ins, 'bindattrs_s', $ins, $type, $name, $exp);

        # Look it up again and output it.
        op(@ins, 'getattrs_s', $got, $ins, $type, $name);
        op(@ins, 'say', $got);
        op(@ins, 'return');
    },
    "omg a kangaroo\n",
    "Can store and look up a string attribute");
