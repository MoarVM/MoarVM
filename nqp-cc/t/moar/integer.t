#!nqp
use MASTTesting;

plan(11);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(101));
        op(@ins, 'coerce_is', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "101\n",
    "integer constant loading");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(34));
        op(@ins, 'const_i64', $r1, ival(8));
        op(@ins, 'add_i', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r3, $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "42\n",
    "integer addition");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(34));
        op(@ins, 'const_i64', $r1, ival(8));
        op(@ins, 'sub_i', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r3, $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "26\n",
    "integer subtraction");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(10));
        op(@ins, 'const_i64', $r1, ival(5));
        op(@ins, 'mul_i', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r3, $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "50\n",
    "integer multiplication");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(-10));
        op(@ins, 'neg_i', $r0, $r0);
        op(@ins, 'coerce_is', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'const_i64', $r0, ival(20));
        op(@ins, 'neg_i', $r0, $r0);
        op(@ins, 'coerce_is', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "10\n-20\n",
    "integer negation");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(23));
        op(@ins, 'const_i64', $r1, ival(23));
        op(@ins, 'const_i64', $r2, ival(555));

        op(@ins, 'eq_i', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'eq_i', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'eq_i', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "1\n0\n0\n",
    "integer equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(23));
        op(@ins, 'const_i64', $r1, ival(23));
        op(@ins, 'const_i64', $r2, ival(555));

        op(@ins, 'ne_i', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ne_i', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ne_i', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "0\n1\n1\n",
    "integer not equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(23));
        op(@ins, 'const_i64', $r1, ival(23));
        op(@ins, 'const_i64', $r2, ival(555));

        op(@ins, 'lt_i', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'lt_i', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'lt_i', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "0\n1\n0\n",
    "integer less than");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(23));
        op(@ins, 'const_i64', $r1, ival(23));
        op(@ins, 'const_i64', $r2, ival(555));

        op(@ins, 'le_i', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'le_i', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'le_i', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "1\n1\n0\n",
    "integer less than or equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(23));
        op(@ins, 'const_i64', $r1, ival(23));
        op(@ins, 'const_i64', $r2, ival(555));

        op(@ins, 'gt_i', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'gt_i', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'gt_i', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "0\n0\n1\n",
    "integer greater than");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(23));
        op(@ins, 'const_i64', $r1, ival(23));
        op(@ins, 'const_i64', $r2, ival(555));

        op(@ins, 'ge_i', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ge_i', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ge_i', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "1\n0\n1\n",
    "integer greater than or equal to");
