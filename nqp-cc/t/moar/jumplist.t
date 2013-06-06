#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
    my $input := const($frame, ival(2));
    op(@ins, 'jumplist', ival(3), $input);
    op(@ins, 'goto', label("bar"));
    op(@ins, 'goto', label("bar"));
    op(@ins, 'goto', label("foo"));
    op(@ins, 'say', const($frame, sval('66')));
    op(@ins, 'return');
    nqp::push(@ins, label("foo"));
    op(@ins, 'say', const($frame, sval('44')));
    op(@ins, 'return');
    nqp::push(@ins, label("bar"));
    op(@ins, 'say', const($frame, sval('55')));
    op(@ins, 'return');
    nqp::push(@ins, label("baz"));
}, "44\n", "jumplist branches");

mast_frame_output_is(-> $frame, @ins, $cu {
    my $input := const($frame, ival(4));
    op(@ins, 'jumplist', ival(3), $input);
    op(@ins, 'goto', label("bar"));
    op(@ins, 'goto', label("bar"));
    op(@ins, 'goto', label("foo"));
    op(@ins, 'say', const($frame, sval('66')));
    op(@ins, 'return');
    nqp::push(@ins, label("foo"));
    op(@ins, 'say', const($frame, sval('44')));
    op(@ins, 'return');
    nqp::push(@ins, label("bar"));
    op(@ins, 'say', const($frame, sval('55')));
    op(@ins, 'return');
    nqp::push(@ins, label("baz"));
}, "66\n", "jumplist out of range");
