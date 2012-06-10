#!nqp
use MASTTesting;

plan(1);

mast_frame_output_is(-> $frame, @ins, $cu {
        my $counter := local($frame, int);
        my $divisor := local($frame, int);
        my $modulus := local($frame, int);
        my $sleepms := local($frame, int);
        op(@ins, 'const_i64', $divisor, ival(10 *1000 *1000));
        op(@ins, 'const_i64', $counter, ival(100 *1000 *1000));
        op(@ins, 'const_i64', $sleepms, ival(1 *1000));
        my $loop := label('loop');
        my $skipsleep := label('skipsleep');
        nqp::push(@ins, $loop);
        op(@ins, 'mod_i', $modulus, $counter, $divisor);
        op(@ins, 'if_i', $modulus, $skipsleep);
        op(@ins, 'sleep', $sleepms);
        nqp::push(@ins, $skipsleep);
        op(@ins, 'dec_i', $counter);
        op(@ins, 'if_i', $counter, $loop);
        op(@ins, 'return');
    },
    "",
    "test memory usage of lots of calling", 1);
