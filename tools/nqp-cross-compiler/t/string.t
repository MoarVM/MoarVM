#!nqp
use MASTTesting;

plan(20);

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
        op(@ins, 'const_s', $r0, sval(''));
        op(@ins, 'const_s', $r1, sval(''));
        op(@ins, 'index_s', $r2, $r0, $r1);
        op(@ins, 'say_i', $r2);
        op(@ins, 'return');
    },
    "0\n",
    "string index both empty");

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

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(0));
        op(@ins, 'const_i64', $r2, ival(3));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "bar\n",
    "string substring full");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(0));
        op(@ins, 'const_i64', $r2, ival(2));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "ba\n",
    "string substring beginning");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(1));
        op(@ins, 'const_i64', $r2, ival(1));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "a\n",
    "string substring middle");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(1));
        op(@ins, 'const_i64', $r2, ival(2));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "ar\n",
    "string substring end");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_s', $r1, sval('foo'));
        op(@ins, 'concat_s', $r0, $r1, $r0);
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    "foobar\n",
    "string concat");

mast_frame_output_is(-> $frame, @ins {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('( « ]> > <term> <.ws>{$¢.add_enum($<na'));
        op(@ins, 'say_s', $r0);
        op(@ins, 'return');
    },
    '( « ]> > <term> <.ws>{$¢.add_enum($<na'~"\n",
    "utf8 round trip");
