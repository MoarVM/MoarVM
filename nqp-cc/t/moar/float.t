#!nqp
use MASTTesting;

plan(13);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(233.232));
        op(@ins, 'coerce_ns', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "233.232\n",
    "float constant loading", approx => 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(-34222.10004));
        op(@ins, 'const_n64', $r1, nval(9993292.1123));
        op(@ins, 'add_n', $r2, $r0, $r1);
        op(@ins, 'coerce_ns', $r3, $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "9959070.01226\n",
    "float addition", approx => 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(3838890000.223));
        op(@ins, 'const_n64', $r1, nval(332424432.22222));
        op(@ins, 'sub_n', $r2, $r0, $r1);
        op(@ins, 'coerce_ns', $r3, $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "3506465568.00078\n",
    "float subtraction", approx => 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(-332233.22333));
        op(@ins, 'const_n64', $r1, nval(382993.23));
        op(@ins, 'mul_n', $r2, $r0, $r1);
        op(@ins, 'coerce_ns', $r3, $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'return');
    },
    "-127243075316.468050\n",
    "float multiplication", approx => 1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(-38838.000033332));
        op(@ins, 'neg_n', $r0, $r0);
        op(@ins, 'coerce_ns', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'const_n64', $r0, nval(33223.22003374));
        op(@ins, 'neg_n', $r0, $r0);
        op(@ins, 'coerce_ns', $r1, $r0);
        op(@ins, 'say', $r1);
        op(@ins, 'return');
    },
    "38838.000033\n-33223.220034\n",
    "float negation");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.5532));
        op(@ins, 'const_n64', $r1, nval(23.5532));
        op(@ins, 'const_n64', $r2, nval(555.33889009));

        op(@ins, 'eq_n', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'eq_n', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'eq_n', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "1\n0\n0\n",
    "float equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.5532));
        op(@ins, 'const_n64', $r1, nval(23.5532));
        op(@ins, 'const_n64', $r2, nval(555.33889009));

        op(@ins, 'ne_n', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ne_n', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ne_n', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "0\n1\n1\n",
    "float not equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.5532));
        op(@ins, 'const_n64', $r1, nval(23.5532));
        op(@ins, 'const_n64', $r2, nval(555.33889009));

        op(@ins, 'lt_n', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'lt_n', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'lt_n', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "0\n1\n0\n",
    "float less than");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.5532));
        op(@ins, 'const_n64', $r1, nval(23.5532));
        op(@ins, 'const_n64', $r2, nval(555.33889009));

        op(@ins, 'le_n', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'le_n', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'le_n', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "1\n1\n0\n",
    "float less than or equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.5532));
        op(@ins, 'const_n64', $r1, nval(23.5532));
        op(@ins, 'const_n64', $r2, nval(555.33889009));

        op(@ins, 'gt_n', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'gt_n', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'gt_n', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "0\n0\n1\n",
    "float greater than");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, num);
        my $r2 := local($frame, num);
        my $r3 := local($frame, int);
        my $r4 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(23.5532));
        op(@ins, 'const_n64', $r1, nval(23.5532));
        op(@ins, 'const_n64', $r2, nval(555.33889009));

        op(@ins, 'ge_n', $r3, $r0, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ge_n', $r3, $r1, $r2);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'ge_n', $r3, $r2, $r1);
        op(@ins, 'coerce_is', $r4, $r3);
        op(@ins, 'say', $r4);

        op(@ins, 'return');
    },
    "1\n0\n1\n",
    "float greater than or equal to");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, num);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        op(@ins, 'const_n64', $r0, nval(16));
        op(@ins, 'coerce_ni', $r1, $r0);
        op(@ins, 'coerce_is', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "16\n",
    "float to int coercion");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, int);
        my $r1 := local($frame, num);
        my $r2 := local($frame, str);
        op(@ins, 'const_i64', $r0, ival(16));
        op(@ins, 'coerce_in', $r1, $r0);
        op(@ins, 'coerce_ns', $r2, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "16\n",
    "int to float coercion", approx => 1);
