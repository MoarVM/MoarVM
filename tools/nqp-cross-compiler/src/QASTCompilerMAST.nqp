use QASTOperationsMAST;

my $MVM_reg_void            := 0; # not really a register; just a result/return kind marker
my $MVM_reg_int8            := 1;
my $MVM_reg_int16           := 2;
my $MVM_reg_int32           := 3;
my $MVM_reg_int64           := 4;
my $MVM_reg_num32           := 5;
my $MVM_reg_num64           := 6;
my $MVM_reg_str             := 7;
my $MVM_reg_obj             := 8;

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
        has %!locals_by_name;   # Mapping of local names to locals
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
        
        method local_type($name) { %!local_types{$name} }
        method lexical_type($name) { %!lexical_types{$name} }
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
    
    multi method as_mast(QAST::Stmts $node) {
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
        MAST::InstructionList.new(@all_ins, $last_stmt.result_reg, $last_stmt.result_kind)
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
    
    multi method as_mast(QAST::Var $node) {
        my $scope := $node.scope;
        my $decl  := $node.decl;
        
        # Handle any declarations; after this, we call through to the
        # lookup code.
        if $decl {
            # If it's a parameter, add it to the things we should bind
            # at block entry.
            if $decl eq 'param' {
                if $scope eq 'local' || $scope eq 'lexical' {
                    $*BLOCK.add_param($node);
                }
                else {
                    nqp::die("Parameter cannot have scope '$scope'; use 'local' or 'lexical'");
                }
            }
            elsif $decl eq 'var' {
                if $scope eq 'local' {
                    $*BLOCK.add_local($node);
                }
                elsif $scope eq 'lexical' {
                    $*BLOCK.add_lexical($node);
                }
                else {
                    nqp::die("Cannot declare variable with scope '$scope'; use 'local' or 'lexical'");
                }
            }
            else {
                nqp::die("Don't understand declaration type '$decl'");
            }
        }
        
        # Now go by scope.
        my $name := $node.name;
        my @ins;
        my $res_reg;
        my $res_type;
        if $scope eq 'local' {
            if $*BLOCK.local_type($name) -> $type {
                if $*BINDVAL {
                    my $valmast := self.as_mast_clear_bindval($*BINDVAL);
                    push_ilist(@ins, $valmast);
                    push_op(@ins, 'set', , $valmast.result_reg);
                }
                
            }
            else {
                nqp::die("Cannot reference undeclared local '$name'");
            }
        }
        elsif $scope eq 'lexical' {
            # If the lexical is directly declared in this block, we use the
            # register directly.
            if $*BLOCK.lexical_type($name) -> $type {
                my $reg := $*BLOCK.lex_reg($name);
                if $*BINDVAL {
                    my $valmast := self.as_mast_clear_bindval($*BINDVAL);
                    nqp::die("NYI 1");
                }
                $res_reg := $reg;
            }
            else {
                # Does the node have a native type marked on it?
                my $type := type_to_register_type($node.returns);
                if $type eq 'P' {
                    # Consider the blocks for a declared native type.
                    # XXX TODO
                }
                
                # Emit the lookup or bind.
                if $*BINDVAL {
                    my $valpost := self.as_mast_clear_bindval($*BINDVAL);
                    nqp::die("NYI 2");
                }
                else {
                    my $res_reg := $*REGALLOC."fresh_{nqp::lc($type)}"();
                    $nqp::die("NYI 3");
                }
            }
        }
        elsif $scope eq 'attribute' {
            # Ensure we have object and class handle.
            my @args := $node.list();
            if +@args != 2 {
                nqp::die("An attribute lookup needs an object and a class handle");
            }
            
            # Compile object and handle.
            my $obj := self.coerce(self.as_post_clear_bindval(@args[0]), 'p');
            my $han := self.coerce(self.as_post_clear_bindval(@args[1]), 'p');
            nqp::die("NYI 4");
            
            # Go by whether it's a bind or lookup.
            my $type    := type_to_register_type($node.returns);
            my $op_type := type_to_lookup_name($node.returns);
            if $*BINDVAL {
                my $valpost := self.as_mast_clear_bindval($*BINDVAL);
                nqp::die("NYI 5");
            }
            else {
                my $res_reg := $*REGALLOC."fresh_{nqp::lc($type)}"();
                nqp::die("NYI 6");
            }
        }
        else {
            nqp::die("QAST::Var with scope '$scope' NYI");
        }
        
        MAST::InstructionList.new(@ins, MAST::VOID, $MVM_reg_void)
    }
    
    method as_mast_clear_bindval($node) {
        my $*BINDVAL := 0;
        self.as_mast($node)
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
            $MVM_reg_int64)
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
            $MVM_reg_num64)
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
            $MVM_reg_str)
    }
}

sub push_op(@dest, $op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("Unable to resolve MAST op '$op'") unless $bank;
    
    nqp::push(@dest, MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    ));
}
