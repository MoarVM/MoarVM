#!nqp
use MASTTesting;

plan(7);

sub hash_type($frame) {
    my @ins := $frame.instructions;
    my $r0 := local($frame, str);
    my $r1 := local($frame, NQPMu);
    my $r2 := local($frame, NQPMu);
    op(@ins, 'const_s', $r0, sval('MVMHash'));
    op(@ins, 'knowhow', $r1);
    op(@ins, 'findmeth', $r2, $r1, sval('new_type'));
    call(@ins, $r2, [$Arg::obj, $Arg::named +| $Arg::str], $r1, sval('repr'), $r0, :result($r1));
    $r1
}

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "0\n",
    "New hash has zero elements");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "1\n",
    "Adding to hash increases element count");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'const_s', $r2, sval('bar'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "2\n",
    "Storage is actually based on the key");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'const_s', $r2, sval('bar'));
        op(@ins, 'existskey', $r1, $r0, $r2);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'existskey', $r1, $r0, $r2);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "0\n1\n",
    "Exists works");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        my $r3 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r0);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r3, $r1);
        op(@ins, 'say', $r3);
        op(@ins, 'deletekey', $r0, $r2);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r3, $r1);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "1\n0\n",
    "Delete works");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        my $r3 := local($frame, NQPMu);
        my $r4 := local($frame, NQPMu);
        my $r5 := local($frame, NQPMu);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'create', $r3, $ht);
        op(@ins, 'create', $r4, $ht);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'bindkey_o', $r0, $r2, $r3);
        op(@ins, 'const_s', $r2, sval('bar'));
        op(@ins, 'bindkey_o', $r0, $r2, $r4);
        op(@ins, 'const_s', $r2, sval('foo'));
        op(@ins, 'atkey_o', $r5, $r0, $r2);
        op(@ins, 'eqaddr', $r1, $r5, $r3);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'eqaddr', $r1, $r5, $r4);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'const_s', $r2, sval('bar'));
        op(@ins, 'atkey_o', $r5, $r0, $r2);
        op(@ins, 'eqaddr', $r1, $r5, $r3);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'eqaddr', $r1, $r5, $r4);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "1\n0\n0\n1\n",
    "Can retrieve items by key");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $ht := hash_type($frame);
        my $r0 := local($frame, NQPMu);
        my $r1 := local($frame, int);
        my $r2 := const($frame, sval("foo"));
        my $r3 := const($frame, sval("bar"));
        my $r4 := const($frame, sval("baz"));
        my $r5 := local($frame, str);
        op(@ins, 'create', $r0, $ht);
        op(@ins, 'bindkey_s', $r0, $r2, $r3);
        op(@ins, 'atkey_s', $r5, $r0, $r2);
        op(@ins, 'say', $r5);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r5, $r1);
        op(@ins, 'say', $r5);
        op(@ins, 'bindkey_s', $r0, $r2, $r4);
        op(@ins, 'atkey_s', $r5, $r0, $r2);
        op(@ins, 'say', $r5);
        op(@ins, 'elems', $r1, $r0);
        op(@ins, 'coerce_is', $r5, $r1);
        op(@ins, 'say', $r5);
        op(@ins, 'return');
    },
    "bar\n1\nbaz\n1\n",
    "associative Replace works");
