#!nqp
use MASTTesting;

plan(2);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type_object := local($frame, NQPMu);
        my $new_type_method := local($frame, NQPMu);
        my $KnowHOW := local($frame, NQPMu);
        my $string_type := local($frame, NQPMu);
        my $ignored := const($frame, sval(""));
        my $io_repr := const($frame, sval('MVMString'));
        my $Stringname := const($frame, sval('MyString'));
        
        op(@ins, 'knowhow', $KnowHOW);
        op(@ins, 'findmeth', $new_type_method, $KnowHOW, sval('new_type'));
        
        call(@ins, $new_type_method,
            [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::str],
            $KnowHOW,
            sval("repr"), $io_repr,
            sval("name"), $Stringname,
            :result($type_object));
        
        my $r0 := const($frame, sval('Makefile'));
        my $r7 := const($frame, ival(1));
        op(@ins, 'slurp', $r0, $type_object, $r0, $r7);
        my $r1 := const($frame, ival(0));
        my $r2 := const($frame, ival(11));
        op(@ins, 'substr_s', $r0, $r0, $r1, $r2);
        op(@ins, 'say', $r0);
        op(@ins, 'return');
    },
    "# Copyright\n",
    "slurp");

# disregard the fact this file is ANSI-encoded but there are UTF-8 chars
# below.  it is because parrot doesn't like to read the UTF-8 encoded
# version of this file.

mast_frame_output_is(-> $frame, @ins, $cu {
        my $type_object := local($frame, NQPMu);
        my $new_type_method := local($frame, NQPMu);
        my $KnowHOW := local($frame, NQPMu);
        my $string_type := local($frame, NQPMu);
        my $ignored := const($frame, sval(""));
        my $io_repr := const($frame, sval('MVMString'));
        my $Stringname := const($frame, sval('MyString'));
        
        op(@ins, 'knowhow', $KnowHOW);
        op(@ins, 'findmeth', $new_type_method, $KnowHOW, sval('new_type'));
        
        call(@ins, $new_type_method,
            [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::str],
            $KnowHOW,
            sval("repr"), $io_repr,
            sval("name"), $Stringname,
            :result($type_object));
        
        my $r0 := const($frame, sval('spewtest.ignore'));
        my $r1 := const($frame, sval("file contents«¢>"));
        my $r2 := local($frame, str);
        my $r7 := const($frame, ival(1));
        op(@ins, 'spew', $r1, $r0, $r7);
        op(@ins, 'slurp', $r2, $type_object, $r0, $r7);
        op(@ins, 'delete_f', $r0);
        op(@ins, 'say', $r2);
        op(@ins, 'return');
    },
    "file contents«¢>\n",
    "spew");

