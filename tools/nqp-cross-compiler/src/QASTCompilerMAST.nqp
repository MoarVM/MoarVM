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
    
    # Holds information about the QAST::Block we're currently compiling.
    my class BlockInfo {
        has $!qast;             # The QAST::Block
        has $!outer;            # Outer block's BlockInfo
        has @!params;           # QAST::Var nodes of params
        has @!locals;           # QAST::Var nodes of declared locals
        has @!lexicals;         # QAST::Var nodes of declared lexicals
        has %!local_types;      # Mapping of local registers to type names
        has %!lexical_types;    # Mapping of lexical names to types
        has %!lexical_regs;     # Mapping of lexical names to registers
        has %!reg_types;        # Mapping of all registers to types
        has int $!param_idx;    # Current lexical parameter index
        
        method new($qast, $outer) {
            my $obj := nqp::create(self);
            $obj.BUILD($qast, $outer);
            $obj
        }
        
        method BUILD($qast, $outer) {
            $!qast := $qast;
            $!outer := $outer;
        }
        
        method add_param($var) {
            if $var.scope eq 'local' {
                self.register_local($var);
            }
            else {
                my $reg := '_lex_param_' ~ $!param_idx;
                $!param_idx := $!param_idx + 1;
                self.register_lexical($var, $reg);
            }
            @!params[+@!params] := $var;
        }
        
        method add_lexical($var) {
            self.register_lexical($var);
            @!lexicals[+@!lexicals] := $var;
        }
        
        method add_local($var) {
            self.register_local($var);
            @!locals[+@!locals] := $var;
        }
        
        method register_lexical($var, $reg?) {
            my $name := $var.name;
            my $type := type_to_register_type($var.returns);
            if nqp::existskey(%!lexical_types, $name) {
                pir::die("Lexical '$name' already declared");
            }
            %!lexical_types{$name} := $type;
            %!lexical_regs{$name} := $reg ?? $reg !! $*BLOCKRA."fresh_{nqp::lc($type)}"();
            %!reg_types{%!lexical_regs{$name}} := $type;
        }
        
        method register_local($var) {
            my $name := $var.name;
            if nqp::existskey(%!local_types, $name) {
                pir::die("Local '$name' already declared");
            }
            %!local_types{$name} := type_to_register_type($var.returns);
            %!reg_types{$name} := %!local_types{$name};
        }
        
        method qast() { $!qast }
        method outer() { $!outer }
        method params() { @!params }
        method lexicals() { @!lexicals }
        method locals() { @!locals }
        
        method lex_reg($name) { %!lexical_regs{$name} }
        
        my %longnames := nqp::hash('P', 'pmc', 'I', 'int', 'N', 'num', 'S', 'string');
        method local_type($name) { %!local_types{$name} }
        method local_type_long($name) { %longnames{%!local_types{$name}} }
        method lexical_type($name) { %!lexical_types{$name} }
        method lexical_type_long($name) { %longnames{%!lexical_types{$name}} }
        method reg_type($name) { %!reg_types{$name} }
    }
    
    our $serno := 0;
    method unique($prefix = '') { $prefix ~ $serno++ }
    
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
    
    # until we get block return types and nested frames working
    multi method as_mast(QAST::FakeBlock $node) {
        self.compile_all_the_stmts($node)
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
    
    multi method as_mast(QAST::Op $node) {
        QAST::MASTOperations.compile_op(self, '', $node)
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
