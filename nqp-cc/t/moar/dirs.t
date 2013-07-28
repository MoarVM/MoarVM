#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := const($frame, sval("testdirs"));
        my $r1 := const($frame, sval("testfile_"));
        my $r2 := const($frame, ival(1));
        my $r3 := const($frame, ival(0o777));
        my $r4 := const($frame, sval("utf8"));
        my $counter := const($frame, ival(4));
        my $index := local($frame, int);
        my $loop := label('loop');
        my $str := local($frame, str);
        op(@ins, 'mkdir', $r0, $r3);
        op(@ins, 'chdir', $r0);

        # create 3 files
        nqp::push(@ins, $loop);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'concat_s', $r1, $r1, const($frame, sval("_")));
        op(@ins, 'spew', const($frame, sval("foo")), $r1, $r4);
        op(@ins, 'if_i', $counter, $loop);

        my $dh := local($frame, NQPMu);
        op(@ins, 'open_dir', $dh, const($frame, sval(".")), $r2);

        my $loop2 := label('loop2');
        my $done := label('done');
        nqp::push(@ins, $loop2);
        # get a directory entry
        op(@ins, 'read_dir', $r1, $dh);
        op(@ins, 'graphs_s', $index, $r1);
        op(@ins, 'unless_i', $index, $done);
        op(@ins, 'index_s', $index, $r1, const($frame, sval(".")), const($frame, ival(0)));
        op(@ins, 'eq_i', $index, $index, const($frame, ival(0)));
        # skip this entry if it starts with '.'
        op(@ins, 'if_i', $index, $loop2);
        op(@ins, 'inc_i', $counter);
        op(@ins, 'delete_f', $r1);
        op(@ins, 'goto', $loop2);

        nqp::push(@ins, $done);
        op(@ins, 'close_dir', $dh);
        op(@ins, 'coerce_is', $str, $counter);
        op(@ins, 'say', $str);
        op(@ins, 'chdir', const($frame, sval("..")));
        op(@ins, 'rmdir', $r0);
        op(@ins, 'return');
    },
    "4\n",
    "dirs test");
