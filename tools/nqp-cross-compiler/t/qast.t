use QASTCompilerMAST;
use MASTCompiler;

my $qast := QAST::Block.new(
    QAST::VM.new(
        moarop => 'say_i',
        QAST::IVal.new( :value(42) )
    )
);

say("QAST -> MAST");
my $mast := QAST::MASTCompiler.to_mast($qast);

say("MAST -> bytecode");
MAST::Compiler.compile($mast, 'answer.moarvm');

say("Written output");
