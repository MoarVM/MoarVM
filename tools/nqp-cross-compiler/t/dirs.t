#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $r0 := const($frame, sval("testdirs"));
        my $r1 := const($frame, sval("testdirs/testfile_"));
        my $r2 := local($frame, str);
        my $counter := const($frame, ival(4));
        my $loop := label('loop');
        op(@ins, 'mkdir', $r0);
        
        # create 3 files
        nqp::push(@ins, $loop);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'concat_s', $r1, $r1, const($frame, sval("_")));
        op(@ins, 'spew', const($frame, sval("foo")), $r1);
        op(@ins, 'if_i', $counter, $loop);
        
        my $dh := local($frame, NQPMu);
        my $osh := local($frame, NQPMu);
        op(@ins, 'anonoshtype', $osh);
        op(@ins, 'open_dir', $dh, $osh, $r0);
        
        my $loop2 := label('loop2');
        my $done := label('done');
        nqp::push(@ins, $loop2);
        # get a directory entry
        op(@ins, 'read_dir', $r1, $dh);
        op(@ins, 'graphs_s', $counter, $r1);
        op(@ins, 'unless_i', $counter, $done);
        op(@ins, 'index_s', $counter, $r1, const($frame, sval(".")));
        op(@ins, 'eq_i', $counter, $counter, const($frame, ival(0)));
        # skip this entry if it starts with '.'
        op(@ins, 'if_i', $counter, $loop2);
        op(@ins, 'say_s', $r1);
        op(@ins, 'concat_s', $r2, const($frame, sval("testdirs/")), $r1);
        op(@ins, 'delete_f', $r2);
        op(@ins, 'goto', $loop2);
        
        nqp::push(@ins, $done);
        op(@ins, 'close_dir', $dh);
        op(@ins, 'rmdir', $r0);
        op(@ins, 'return');
    },
    "testfile__\ntestfile___\ntestfile____\ntestfile_____\n",
    "dirs test");
