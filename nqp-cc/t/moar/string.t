#!nqp
use MASTTesting;

plan(40);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('OMG strings!'));
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "OMG strings!\n",
    "string constant loading");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('baz'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "-1\n",
    "string index no match");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('baz'));
        op(@ins, 'const_s', $r1, sval('foobar'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "-1\n",
    "string index bigger");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval(''));
        op(@ins, 'const_s', $r1, sval('foobar'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "-1\n",
    "string index haystack empty");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval(''));
        op(@ins, 'const_s', $r1, sval(''));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "string index both empty");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval(''));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "-1\n",
    "string index needle empty");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('my $r0 := local($frame, str)'));
        op(@ins, 'const_s', $r1, sval('my $r0 := local($frame, str)'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "string index equals");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('foo'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "string index beginning");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('oob'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "1\n",
    "string index 1");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'index_s', $r2, $r0, $r1, const($frame, ival(0)));
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "3\n",
    "string index end");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'eq_s', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "string equal not");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'eq_s', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "1\n",
    "string equal");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'ne_s', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "1\n",
    "string not equal");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_s', $r1, sval('bar'));
        op(@ins, 'ne_s', $r2, $r0, $r1);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "string not equal not");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(0));
        op(@ins, 'const_i64', $r2, ival(3));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "bar\n",
    "string substring full");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(0));
        op(@ins, 'const_i64', $r2, ival(2));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "ba\n",
    "string substring beginning");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(1));
        op(@ins, 'const_i64', $r2, ival(1));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "a\n",
    "string substring middle");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(1));
        op(@ins, 'const_i64', $r2, ival(2));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "ar\n",
    "string substring end");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_s', $r1, sval('foo'));
        op(@ins, 'concat_s', $r0, $r1, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "foobar\n",
    "string concat");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(4));
        op(@ins, 'repeat_s', $r0, $r0, $r1);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "barbarbarbar\n", # doin' it like a barbarian
    "string repeat");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('( « ]> > <term> <.ws>{$¢.add_enum($<na'));
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    '( « ]> > <term> <.ws>{$¢.add_enum($<na'~"\n",
    "string utf8 round trip");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('oba'));
        op(@ins, 'const_i64', $r2, ival(2));
        op(@ins, 'eqat_s', $r2, $r0, $r1, $r2);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "1\n",
    "string eqat true");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('oba'));
        op(@ins, 'const_i64', $r2, ival(1));
        op(@ins, 'eqat_s', $r2, $r0, $r1, $r2);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "0\n",
    "string eqat false");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, str);
        my $r2 := local($frame, int);
        my $r3 := local($frame, int);
        my $r4 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('foobar'));
        op(@ins, 'const_s', $r1, sval('oba'));
        op(@ins, 'const_i64', $r2, ival(2));
        op(@ins, 'const_i64', $r3, ival(3));
        op(@ins, 'const_i64', $r4, ival(0));
        op(@ins, 'haveat_s', $r2, $r0, $r2, $r3, $r1, $r4);
        op(@ins, 'coerce_is', $r0, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "1\n",
    "string haveat true");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('( « ]> > <term> <.ws>{$¢.add_enum($<na'));
        op(@ins, 'const_i64', $r1, ival(2));
        op(@ins, 'getcp_s', $r1, $r0, $r1);
        op(@ins, 'coerce_is', $r0, $r1);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "171\n",
    "string get codepoint at index");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('«'));
        op(@ins, 'const_i64', $r1, ival(171));
        op(@ins, 'repeat_s', $r0, $r0, $r1);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    nqp::x("«", 171) ~ "\n",
    "string repeat with non-ASCII char");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        op(@ins, 'const_s', $r0, sval('(  ]> > <term> <.ws>{$¢«.add_enum($<na'));
        op(@ins, 'const_i64', $r1, ival(171));
        op(@ins, 'indexcp_s', $r1, $r0, $r1);
        op(@ins, 'coerce_is', $r0, $r1);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "23\n",
    "string index of codepoint");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('beer'));
        op(@ins, 'say', $r0);
        op(@ins, 'uc', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "beer\nBEER\n",
    "uppercase in the ASCII range");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('CHEESE'));
        op(@ins, 'say', $r0);
        op(@ins, 'lc', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "CHEESE\ncheese\n",
    "lowercase in the ASCII range");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('пиво'));
        op(@ins, 'say', $r0);
        op(@ins, 'uc', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "пиво\nПИВО\n",
    "uppercase beyond ASCII range");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('СЫР'));
        op(@ins, 'say', $r0);
        op(@ins, 'lc', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "СЫР\nсыр\n",
    "lowercase beyond ASCII range");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('ǉ'));
        op(@ins, 'say', $r0);
        op(@ins, 'tc', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'uc', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "ǉ\nǈ\nǇ\n",
    "titlecase works and can be distinct from uppercase");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $input_str := const($frame, sval("\n\n\n\n\nfoo\n\nbar\nbaz\n"));
    my $separator := const($frame, sval("\n"));
    my $arr := local($frame, NQPMu);
    op(@ins, 'split', $arr, $separator, $input_str);

    my $elems := local($frame, int);
    my $str := local($frame, str);
    op(@ins, 'elems', $elems, $arr);
    op(@ins, 'coerce_is', $str, $elems);
    op(@ins, 'say', $str);

    my $item_o := local($frame, NQPMu);
    my $item := local($frame, str);
    op(@ins, 'atpos_o', $item_o, $arr, const($frame, ival(5)));
    op(@ins, 'unbox_s', $item, $item_o);
    op(@ins, 'say', $item);
    op(@ins, 'atpos_o', $item_o, $arr, const($frame, ival(8)));
    op(@ins, 'unbox_s', $item, $item_o);
    op(@ins, 'say', $item);
    op(@ins, 'return');
}, "10\nfoo\nbaz\n", "string split multiple separators together");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $input_str := const($frame, sval("foo\n\nbar\nbaz\n"));
    my $separator := const($frame, sval(""));
    my $arr := local($frame, NQPMu);
    op(@ins, 'split', $arr, $separator, $input_str);

    my $elems := local($frame, int);
    my $str := local($frame, str);
    op(@ins, 'elems', $elems, $arr);
    op(@ins, 'coerce_is', $str, $elems);
    op(@ins, 'say', $str);

    my $item_o := local($frame, NQPMu);
    my $item := local($frame, str);
    op(@ins, 'atpos_o', $item_o, $arr, const($frame, ival(0)));
    op(@ins, 'unbox_s', $item, $item_o);
    op(@ins, 'say', $item);
    op(@ins, 'atpos_o', $item_o, $arr, const($frame, ival(12)));
    op(@ins, 'unbox_s', $item, $item_o);
    op(@ins, 'say', $item);
    op(@ins, 'return');
}, "13\nf\n\n\n", "string split empty separator");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $input_str := const($frame, sval(""));
    my $separator := const($frame, sval("\n"));
    my $arr := local($frame, NQPMu);
    op(@ins, 'split', $arr, $separator, $input_str);

    my $elems := local($frame, int);
    my $str := local($frame, str);
    op(@ins, 'elems', $elems, $arr);
    op(@ins, 'coerce_is', $str, $elems);
    op(@ins, 'say', $str);

    op(@ins, 'return');
}, "0\n", "string split empty input");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $arr := local($frame, NQPMu);
    my $input_str := const($frame, sval("foo\n\nbar\nbaz\n"));
    my $output_str := local($frame, str);
    my $delimiter := const($frame, sval("\n"));
    op(@ins, 'split', $arr, $delimiter, $input_str);
    op(@ins, 'join', $output_str, $delimiter, $arr);
    op(@ins, 'say', $output_str);
}, "foo\n\nbar\nbaz\n\n", "join basic");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $arr := local($frame, NQPMu);
    my $input_str := const($frame, sval("\n"));
    my $output_str := local($frame, str);
    my $delimiter := const($frame, sval("\n"));
    op(@ins, 'split', $arr, $delimiter, $input_str);
    op(@ins, 'join', $output_str, $delimiter, $arr);
    op(@ins, 'say', $output_str);
}, "\n\n", "join (almost) empty array");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $arr := local($frame, NQPMu);
    my $input_str := const($frame, sval("foo\n\nbar\nbaz\n"));
    my $output_str := local($frame, str);
    my $delimiter := const($frame, sval("\n"));
    op(@ins, 'split', $arr, $delimiter, $input_str);
    op(@ins, 'set', $delimiter, const($frame, sval("")));
    op(@ins, 'join', $output_str, $delimiter, $arr);
    op(@ins, 'say', $output_str);
}, "foobarbaz\n", "join empty delimiter");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $null := local($frame, NQPMu);
    my $arr := local($frame, NQPMu);
    my $input_str := const($frame, sval("foo\n\nbar\nbaz\n"));
    my $output_str := local($frame, str);
    my $delimiter := const($frame, sval("\n"));
    op(@ins, 'split', $arr, $delimiter, $input_str);
    op(@ins, 'bindpos_o', $arr, const($frame, ival(2)), $null);
    op(@ins, 'set', $delimiter, const($frame, sval("")));
    op(@ins, 'join', $output_str, $delimiter, $arr);
    op(@ins, 'say', $output_str);
}, "foobaz\n", "join sparse array");

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := local($frame, str);
        my $r1 := local($frame, int);
        my $r2 := local($frame, str);
        my $r3 := local($frame, str);
        op(@ins, 'const_s', $r0, sval('bar'));
        op(@ins, 'const_i64', $r1, ival(4));
        op(@ins, 'repeat_s', $r2, $r0, $r1);
        op(@ins, 'repeat_s', $r2, $r2, $r1);
        op(@ins, 'uc', $r2, $r2);
        op(@ins, 'const_s', $r0, sval('BARBAR'));
        op(@ins, 'const_i64', $r1, ival(2));
        op(@ins, 'repeat_s', $r3, $r0, $r1);
        op(@ins, 'const_i64', $r1, ival(4));
        op(@ins, 'repeat_s', $r3, $r3, $r1);
        op(@ins, 'say', $r2);
        op(@ins, 'say', $r3);
        op(@ins, 'eq_s', $r1, $r2, $r3);
        op(@ins, 'coerce_is', $r0, $r1);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "BARBARBARBARBARBARBARBARBARBARBARBARBARBARBARBAR\n"~
    "BARBARBARBARBARBARBARBARBARBARBARBARBARBARBARBAR\n1\n",
    "equals of tree of UPPERed string tree");
