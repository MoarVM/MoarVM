#!nqp
use MASTTesting;

plan(2);

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'say',
        QAST::VM.new( moarop => 'hasuniprop',
            QAST::SVal.new( :value("\xFFFC") ), # see line 1841 of DerivedLineBreak.txt
            QAST::IVal.new( :value(0) ),
            QAST::VM.new( moarop => 'unipropcode',
                QAST::SVal.new( :value("lb"))
            ),
            QAST::VM.new( moarop => 'unipvalcode',
                QAST::VM.new( moarop => 'unipropcode',
                    QAST::SVal.new( :value("lb"))
                ),
                QAST::SVal.new( :value("cb"))
            )
        )
    )
), "1\n", "unicode property value lookup property name and property value name alias");

qast_output_is(QAST::Block.new(
    QAST::Op.new( op => 'say',
        QAST::VM.new( moarop => 'hasuniprop',
            QAST::SVal.new( :value("a") ), # see line 1841 of DerivedLineBreak.txt
            QAST::IVal.new( :value(0) ),
            QAST::VM.new( moarop => 'unipropcode',
                QAST::SVal.new( :value("Alpha"))
            ),
            QAST::IVal.new( :value(1) )
        )
    ),
    QAST::Op.new( op => 'say',
        QAST::VM.new( moarop => 'hasuniprop',
            QAST::SVal.new( :value("1") ), # see line 1841 of DerivedLineBreak.txt
            QAST::IVal.new( :value(0) ),
            QAST::VM.new( moarop => 'unipropcode',
                QAST::SVal.new( :value("Numeric_Type"))
            ),
            QAST::VM.new( moarop => 'unipvalcode',
                QAST::VM.new( moarop => 'unipropcode',
                    QAST::SVal.new( :value("Numeric_Type"))
                ),
                QAST::SVal.new( :value("Decimal"))
            )
        )
    )
), "1\n1\n", "unicode Alpha and Numeric_Type=Decimal property");
