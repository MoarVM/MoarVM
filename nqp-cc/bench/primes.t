#!nqp
use MASTTesting;

plan(1);

sub primes($upto) {
    mast_frame_output_is(-> $frame, @ins, $cu {
        my $i1 := const($frame, ival(1));
        my $i2 := const($frame, ival($upto));
        my $i3 := local($frame, int);
        my $i4 := local($frame, int);
        my $i5 := local($frame, int);
        my $i6 := const($frame, ival(0));
        my $i7 := local($frame, int);
        my $two := const($frame, ival(2));
        my $i8 := const($frame, ival(0));
        my $str := const($frame, sval("N primes up to"));
        my $REDO := label('REDO');
        my $LOOP := label('LOOP');
        my $NEXT := label('NEXT');
        my $OK := label('OK');

        op(@ins, 'say', $str);
        op(@ins, 'const_i64', $i8, ival($upto));
        op(@ins, 'coerce_is', $str, $i8);
        op(@ins, 'say', $str);
        op(@ins, 'const_s', $str, sval(" is: "));
        op(@ins, 'say', $str);

    nqp::push(@ins, $REDO);
        op(@ins, 'set', $i3, $two);
        op(@ins, 'div_i', $i4, $i1, $two);
    nqp::push(@ins, $LOOP);
        op(@ins, 'mod_i', $i5, $i1, $i3);
        op(@ins, 'if_i', $i5, $OK);
        op(@ins, 'goto', $NEXT);
    nqp::push(@ins, $OK);
        op(@ins, 'inc_i', $i3);
        op(@ins, 'le_i', $i8, $i3, $i4);
        op(@ins, 'if_i', $i8, $LOOP);
        op(@ins, 'inc_i', $i6);
        op(@ins, 'set', $i7, $i1);
    nqp::push(@ins, $NEXT);
        op(@ins, 'inc_i', $i1);
        op(@ins, 'le_i', $i8, $i1, $i2);
        op(@ins, 'if_i', $i8, $REDO);
        op(@ins, 'coerce_is', $str, $i6);
        op(@ins, 'say', $str);
        op(@ins, 'const_s', $str, sval("last is: "));
        op(@ins, 'say', $str);
        op(@ins, 'coerce_is', $str, $i7);
        op(@ins, 'say', $str);

    },
    "",
    "prime generation iterative", 1);
}

primes(17619);
