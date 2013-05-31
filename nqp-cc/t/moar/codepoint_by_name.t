#!nqp
use MASTTesting;

plan(2);

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'say',
        QAST::VM.new( moarop => 'getcpbyname',
            QAST::SVal.new( :value("TAI LE LETTER TSHA"))
        )
    )
), "6497\n", "codepoint from name success");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'say',
        QAST::VM.new( moarop => 'getcpbyname',
            QAST::SVal.new( :value("JNTHNWRTHNGTN"))
        )
    )
), "-1\n", "codepoint from name failure");
