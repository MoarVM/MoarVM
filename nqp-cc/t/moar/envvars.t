#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := const($frame, sval('FOOFOO'));
        op(@ins, 'getenv', $r0, $r0);
        op(@ins, 'say', $r0);
        op(@ins, 'setenv', const($frame, sval('FOOFOO')), const($frame, sval('foobar')));
        op(@ins, 'getenv', $r0, const($frame, sval('FOOFOO')));
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "\nfoobar\n",
    "set get env vars");
