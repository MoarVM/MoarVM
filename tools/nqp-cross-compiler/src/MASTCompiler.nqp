use MASTNodes;
use MASTOps;

class MAST::Compiler {
    method compile($node, $target_file) {
        pir::mvm_compiler_setup__vPPPPPPPPP(
            MAST::CompUnit,
            MAST::Frame,
            MAST::Op,
            MAST::SVal,
            MAST::IVal,
            MAST::NVal,
            MAST::Label,
            MAST::Local,
            MAST::Lexical,
            MAST::Call);
        pir::mvm_compile__vPs($node, $target_file);
    }
}
