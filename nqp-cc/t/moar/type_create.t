#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type_object := local($frame, NQPMu);
        my $new_type_method := local($frame, NQPMu);
        my $KnowHOW := local($frame, NQPMu);
        my $test_io_type := local($frame, NQPMu);
        my $ignored := const($frame, sval(""));
        my $io_repr := const($frame, sval('MVMOSHandle'));
        my $TestIOname := const($frame, sval('TestIO'));

        op(@ins, 'knowhow', $KnowHOW);
        op(@ins, 'findmeth', $new_type_method, $KnowHOW, sval('new_type'));

        call(@ins, $new_type_method,
            [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::str],
            $KnowHOW,
            sval("repr"), $io_repr,
            sval("name"), $TestIOname,
            :result($type_object));


        op(@ins, 'say', const($frame, sval("alive")));
        op(@ins, 'return');
    },
    "alive\n",
    "object creation");
