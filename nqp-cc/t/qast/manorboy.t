#!nqp
use MASTTesting;

my $test_depth := 16;

plan($test_depth);

my @answers := [
    1, 0, -2, 0, 1, 0, 1, -1, -10, -30,
    -67, -138, -291, -642, -1446, -3250, -7244, -16065, -35601, -78985,
    -175416, -389695, -865609, -1922362, -4268854, -9479595 ];

sub lex_param($name, $type) {
    QAST::Var.new(
        :name($name),
        :scope('lexical'), :decl('param'), :returns($type) )
}

sub lex($name) {
    QAST::Var.new( :name($name), :scope('lexical') )
}

my $i := 1;
while $i <= $test_depth {

    my $Ksub := QAST::Block.new(
        lex('$n')
    );

    my $K := QAST::Block.new(
        lex_param('$n', int),
        $Ksub,
        QAST::VM.new(
            :moarop('takeclosure'),
            QAST::BVal.new( :value($Ksub) ) )
    );

    my $Bsub := QAST::Block.new(
        QAST::Op.new(
            :op('bind'),
            lex('$k'),
            QAST::VM.new(
                :moarop('sub_i'),
                lex('$k'),
                QAST::IVal.new( :value(1) ) ) ),
        QAST::Op.new(
            :op('call'), :returns(int),
            lex('&A'),
            lex('$k'),
            lex('&B'),
            lex('$x1'),
            lex('$x2'),
            lex('$x3'),
            lex('$x4') )
    );

    my $A := QAST::Block.new(
        lex_param('$k', int),
        lex_param('$x1', NQPMu),
        lex_param('$x2', NQPMu),
        lex_param('$x3', NQPMu),
        lex_param('$x4', NQPMu),
        lex_param('$x5', NQPMu),
        QAST::Var.new(
            :name('&B'), :scope('lexical'), :decl('var') ),
        $Bsub,
        QAST::Op.new(
            :op('bind'),
            lex('&B'),
            QAST::VM.new(
                :moarop('takeclosure'),
                QAST::BVal.new( :value($Bsub) ) ) ),
        QAST::Op.new(
            :op('if'),
            QAST::VM.new(
                :moarop('le_i'),
                lex('$k'),
                QAST::IVal.new( :value(0) ) ),
            QAST::VM.new(
                :moarop('add_i'),
                QAST::Op.new(
                    :op('call'), :returns(int),
                    lex('$x4') ),
                QAST::Op.new(
                    :op('call'), :returns(int),
                    lex('$x5') ) ),
            QAST::Op.new(
                :op('call'), :returns(int),
                lex('&B') ) )
    );

    sub Kcall($val) {
        QAST::Op.new(
            :op('call'),
            lex('&K'),
            QAST::IVal.new( :value($val) ) )
    }

    my $main := QAST::Block.new(
        QAST::Var.new( :name('&K'), :scope('lexical'), :decl('var') ),
        QAST::Var.new( :name('&A'), :scope('lexical'), :decl('var') ),
        $K,
        $A,
        QAST::Op.new(
            :op('bind'),
            lex('&K'),
            QAST::VM.new( :moarop('takeclosure'), QAST::BVal.new( :value($K) ) )
        ),
        QAST::Op.new(
            :op('bind'),
            lex('&A'),
            QAST::VM.new( :moarop('takeclosure'), QAST::BVal.new( :value($A) ) )
        ),
        QAST::VM.new(
            :moarop('say'),
            QAST::VM.new(
            	:moarop('coerce_is'),
				QAST::Op.new(
					:op('call'), :returns(int),
					lex('&A'),
					QAST::IVal.new( :value($i) ), # <-- here is the loop variable
					Kcall(1), Kcall(-1), Kcall(-1), Kcall(1), Kcall(0) ) ) )
    );

    my $expected := @answers[$i];
    qast_output_is($main, "$expected\n", "Knuth man or boy $i");
    $i++;
}

# The equivalent (known working) perlesque code, for reference:
#
# sub A(int $k, r_int $x1, r_int $x2, r_int $x3, r_int $x4, r_int $x5 --> int) {
#   my r_int &B = sub (--> int) { $k = $k - 1; return A($k, &B, $x1, $x2, $x3, $x4) };
#   if ($k <= 0) { return $x4() + $x5() }
#   return &B()
# }
#
# sub K(int $n --> r_int) { return sub (--> int) { return $n } }
#
# loop (my $i = 1; $i <= $test_depth ; $i = $i + 1 ) {
#   my $result = A($i, K(1), K(-1), K(-1), K(1), K(0) );
#   my $expected = $answers[$i];
#   if ($result != $expected) { say("not ok # got " ~ $result ~ " but expected " ~ $expected) }
#   else { say("ok " ~ $i ~ " # Knuth's man_or_boy test at starting value " ~ $i ~ " got " ~ $result) }
# }
#
