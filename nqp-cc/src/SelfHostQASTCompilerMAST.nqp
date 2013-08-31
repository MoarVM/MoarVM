class MASTBytecodeAssembler {
    method node_hash() {
        nqp::hash(
            'CompUnit',         MAST::CompUnit,
            'Frame',            MAST::Frame,
            'Op',               MAST::Op,
            'SVal',             MAST::SVal,
            'IVal',             MAST::IVal,
            'NVal',             MAST::NVal,
            'Label',            MAST::Label,
            'Local',            MAST::Local,
            'Lexical',          MAST::Lexical,
            'Call',             MAST::Call,
            'Annotated',        MAST::Annotated,
            'HandlerScope',     MAST::HandlerScope
        )
    }
    
    method assemble_to_file($mast, $file) {
        __MVM__masttofile($mast, self.node_hash(), $file)
    }
    
    method assemble_and_load($mast) {
        __MVM__masttocu($mast, self.node_hash())
    }
}

nqp::bindcomp('MAST', MASTBytecodeAssembler);
