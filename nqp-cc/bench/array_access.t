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

sub array_type($frame) {
    my @ins := $frame.instructions;
    my $r0 := local($frame, str);
    my $r1 := local($frame, NQPMu);
    my $r2 := local($frame, NQPMu);
    op(@ins, 'const_s', $r0, sval('MVMArray'));
    op(@ins, 'knowhow', $r1);
    op(@ins, 'findmeth', $r2, $r1, sval('new_type'));
    call(@ins, $r2, [$Arg::obj, $Arg::named +| $Arg::str], $r1, sval('repr'), $r0, :result($r1));
    $r1
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

sub _bench($arr_size_opt, $iter_opt) {
    mast_frame_output_is(-> $frame, @ins, $cu {
        my $arr_type := array_type($frame);
        my $int_type := boxing_type($frame, 'Int');
        my $x_index := const($frame, ival(0));
        my $value := const($frame, ival(1));
        my $start_time := local($frame, int);
        my $end_time := local($frame, int);
        my $arr_1 := local($frame, NQPMu);
        my $arr_2 := local($frame, NQPMu);
        my $arr_size := local($frame, int);
        my $i0 := local($frame, int);
        my $X_LOOP := label("X_LOOP");
        my $X_DONE := label("X_DONE");
        my $elem := local($frame, NQPMu);
        my $boxed_zero := local($frame, NQPMu);
        my $zero := const($frame, ival(0));
        my $one := const($frame, ival(1));

        my $max_index := const($frame, ival($arr_size_opt - 1));
        my $y_index := const($frame, ival(0));
        my $z_index := local($frame, int);
        my $iterations := const($frame, ival($iter_opt));
        my $Y_LOOP := label("Y_LOOP");
        my $Z_LOOP := label("Z_LOOP");
        my $Z_DONE := label("Z_DONE");
        my $Y_DONE := label("Y_DONE");
        my $i3 := local($frame, int);
        my $i4 := local($frame, int);
        my $str := local($frame, str);

        op(@ins, 'const_i64', $arr_size, ival($arr_size_opt));
        op(@ins, 'create', $arr_1, $arr_type);
        op(@ins, 'create', $arr_2, $arr_type);
        op(@ins, 'setelemspos', $arr_1, $arr_size);
        op(@ins, 'setelemspos', $arr_2, $arr_size);

        op(@ins, 'time_i', $start_time);
        op(@ins, 'box_i', $boxed_zero, $zero, $int_type);

    nqp::push(@ins, $X_LOOP);
        op(@ins, 'ge_i', $i0, $x_index, $arr_size);
        op(@ins, 'if_i', $i0, $X_DONE);
        op(@ins, 'box_i', $elem, $value, $int_type);
        op(@ins, 'bindpos_o', $arr_1, $x_index, $elem);
        op(@ins, 'bindpos_o', $arr_2, $x_index, $boxed_zero);
        op(@ins, 'inc_i', $x_index);
        op(@ins, 'inc_i', $value);
        op(@ins, 'goto', $X_LOOP);
    nqp::push(@ins, $X_DONE);

        op(@ins, 'time_i', $end_time);
        op(@ins, 'sub_i', $i3, $end_time, $start_time);
        op(@ins, 'coerce_is', $str, $i3);
        op(@ins, 'say', $str);

    nqp::push(@ins, $Y_LOOP);
        op(@ins, 'ge_i', $i0, $y_index, $iterations);
        op(@ins, 'if_i', $i0, $Y_DONE);
        op(@ins, 'set', $z_index, $max_index);
    nqp::push(@ins, $Z_LOOP);
        op(@ins, 'lt_i', $i0, $z_index, $zero);
        op(@ins, 'if_i', $i0, $Z_DONE);
        op(@ins, 'atpos_o', $elem, $arr_2, $z_index);
        op(@ins, 'unbox_i', $i3, $elem);
        op(@ins, 'atpos_o', $elem, $arr_1, $z_index);
        op(@ins, 'unbox_i', $i4, $elem);
        op(@ins, 'add_i', $i3, $i3, $i4);
        op(@ins, 'box_i', $elem, $i3, $int_type);
        op(@ins, 'bindpos_o', $arr_2, $z_index, $elem);
        op(@ins, 'dec_i', $z_index);
        op(@ins, 'goto', $Z_LOOP);
    nqp::push(@ins, $Z_DONE);

        op(@ins, 'inc_i', $y_index);
        op(@ins, 'goto', $Y_LOOP);
    nqp::push(@ins, $Y_DONE);

        op(@ins, 'time_i', $end_time);
        op(@ins, 'sub_i', $i3, $end_time, $start_time);
        op(@ins, 'coerce_is', $str, $i3);
        op(@ins, 'say', $str);
    },
    "",
    "array accesses "~($arr_size_opt * $iter_opt) ~ " of boxed and unboxed ints", 1);
}

_bench(10000, 10000);
