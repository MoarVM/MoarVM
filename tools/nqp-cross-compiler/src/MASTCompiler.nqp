use MASTNodes;
use MASTOps;

class MAST::Compiler {
    method compile($node, $target_file) {
        pir::mvm_compiler_setup__vPPPPPPPPP(
            MAST::CompUnit,
            MAST::Frame,
            MAST::Op,
            MAST::StrLit,
            MAST::IntLit,
            MAST::NumLit,
            MAST::Label,
            MAST::Local,
            MAST::Lexical);
        say("NYI");
    }
}
