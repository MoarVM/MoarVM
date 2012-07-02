use QASTOperationsMAST;

class QAST::MASTCompiler {
    # This isn't really doing allocation at all right now...
    my class RegAlloc {
        has $!frame;
        
        method new($frame) {
            my $obj := nqp::create(self);
            nqp::bindattr($obj, RegAlloc, '$!frame', $frame);
            $obj
        }
        
        method fresh_o() {
            MAST::Local.new($!frame.add_local(NQPMu))
        }
        
        method fresh_i() {
            MAST::Local.new($!frame.add_local(int))
        }
        
        method fresh_n() {
            MAST::Local.new($!frame.add_local(num))
        }
        
        method fresh_s() {
            MAST::Local.new($!frame.add_local(str))
        }
    }
    
    method to_mast($qast) {
        my $*MAST_COMPUNIT := MAST::CompUnit.new();
        self.as_mast($qast);
        $*MAST_COMPUNIT
    }
    
    proto method as_mast($qast) { * }
    
    multi method as_mast(QAST::Block $node) {
        # Create an empty frame and add it to the compilation unit.
        my $*MAST_FRAME := MAST::Frame.new(:name('xxx'), :cuuid('yyy'));
        $*MAST_COMPUNIT.add_frame($*MAST_FRAME);
        
        # Create a register allocator for this frame.
        my $*REGALLOC := RegAlloc.new($*MAST_FRAME);

        # Compile all the substatements.
        my $ins := self.compile_all_the_stmts($node);

        # Add to instructions list for this block.
        # XXX Last thing is return value, later...
        nqp::splice($*MAST_FRAME.instructions, $ins.instructions, 0, 0);
    }
    
    # This takes any node that is a statement list of some kind and compiles
    # all of the statemnets within it.
    method compile_all_the_stmts($node) {
        my @all_ins;
        my $last_stmt;
        for @($node) {
            # Compile this child to MAST, and add its instructions to the end
            # of our instruction list. Also track the last sttatement.
            $last_stmt := self.as_mast($_);
            nqp::splice(@all_ins, $last_stmt.instructions, +@all_ins, 0);
        }
        MAST::InstructionList.new(@all_ins, $last_stmt.result_reg, $last_stmt.result_type)
    }
    
    multi method as_mast(QAST::VM $vm) {
        if $vm.supports('moarop') {
            QAST::MASTOperations.compile_mastop(self, $vm.alternative('moarop'), $vm.list)
        }
        else {
            nqp::die("To compile on the MoarVM backend, QAST::VM must have an alternative 'moarop'");
        }
    }
    
    multi method as_mast(QAST::IVal $iv) {
        my $reg := $*REGALLOC.fresh_i();
        MAST::InstructionList.new(
            [MAST::Op.new(
                :bank('primitives'), :op('const_i64'),
                $reg,
                MAST::IVal.new( :value($iv.value) )
            )],
            $reg,
            int)
    }
}
