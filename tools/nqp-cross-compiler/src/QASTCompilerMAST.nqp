use QASTOperationsMAST;

class QAST::MASTCompiler {
    # This uses a very simple scheme. Write registers are assumed
    # to be write-once, read-once.  Therefore, if a QAST control
    # structure wants to reuse the intermediate result of an
    # expression, it must `set` the result to other registers before
    # using the result as an arg to another op.
    my class RegAlloc {
        has $!frame;
        has @!objs;
        has @!ints;
        has @!nums;
        has @!strs;
        
        method new($frame) {
            my $obj := nqp::create(self);
            nqp::bindattr($obj, RegAlloc, '$!frame', $frame);
            nqp::bindattr($obj, RegAlloc, '@!objs', []);
            nqp::bindattr($obj, RegAlloc, '@!ints', []);
            nqp::bindattr($obj, RegAlloc, '@!nums', []);
            nqp::bindattr($obj, RegAlloc, '@!strs', []);
            $obj
        }
        
        method fresh_o() {
            nqp::elems(@!objs) ?? nqp::pop(@!objs) !! MAST::Local.new($!frame.add_local(NQPMu))
        }
        
        method fresh_i() {
            nqp::elems(@!ints) ?? nqp::pop(@!ints) !! MAST::Local.new($!frame.add_local(int))
        }
        
        method fresh_n() {
            nqp::elems(@!nums) ?? nqp::pop(@!nums) !! MAST::Local.new($!frame.add_local(num))
        }
        
        method fresh_s() {
            nqp::elems(@!strs) ?? nqp::pop(@!strs) !! MAST::Local.new($!frame.add_local(str))
        }
        
        method release_o($reg) { nqp::push(@!objs, $reg) }
        method release_i($reg) { nqp::push(@!ints, $reg) }
        method release_n($reg) { nqp::push(@!nums, $reg) }
        method release_s($reg) { nqp::push(@!strs, $reg) }
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
    # all of the statements within it.
    method compile_all_the_stmts($node) {
        my @all_ins;
        my $last_stmt;
        for @($node) {
            # Compile this child to MAST, and add its instructions to the end
            # of our instruction list. Also track the last statement.
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
    
    multi method as_mast(QAST::NVal $nv) {
        my $reg := $*REGALLOC.fresh_n();
        MAST::InstructionList.new(
            [MAST::Op.new(
                :bank('primitives'), :op('const_n64'),
                $reg,
                MAST::NVal.new( :value($nv.value) )
            )],
            $reg,
            num)
    }
    
    multi method as_mast(QAST::SVal $sv) {
        my $reg := $*REGALLOC.fresh_s();
        MAST::InstructionList.new(
            [MAST::Op.new(
                :bank('primitives'), :op('const_s'),
                $reg,
                MAST::SVal.new( :value($sv.value) )
            )],
            $reg,
            str)
    }
}
