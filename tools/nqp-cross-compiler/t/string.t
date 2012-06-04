#!nqp
use MASTTesting;

plan(13);

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('OMG strings!'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "OMG strings!\n",
    "string constant loading");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('baz'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "-1\n",
    "string index no match");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('baz'));
        op(@ins, 'const_s', $r1, sval('foobar'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "-1\n",
    "string index bigger");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval(''));
        op(@ins, 'const_s', $r1, sval('foobar'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "-1\n",
    "string index haystack empty");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval(''));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "-1\n",
    "string index needle empty");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('my $r0 := local($frame, str)'));
        op(@ins, 'const_s', $r1, sval('my $r0 := local($frame, str)'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "0\n",
    "string index equals");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('foo'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "0\n",
    "string index beginning");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('oob'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "1\n",
    "string index 1");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "3\n",
    "string index end");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'eq_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "0\n",
    "string equal not");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'eq_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "1\n",
    "string equal");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'ne_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "1\n",
    "string not equal");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'ne_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "0\n",
    "string not equal not");