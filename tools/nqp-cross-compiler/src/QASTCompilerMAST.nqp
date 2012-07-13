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
        
        method fresh_i() { self.fresh_register($MVM_reg_int64) }
        method fresh_n() { self.fresh_register($MVM_reg_num64) }
        method fresh_s() { self.fresh_register($MVM_reg_str) }
        method fresh_o() { self.fresh_register($MVM_reg_obj) }
        
        # QAST::Vars need entirely new MAST::Locals all to themselves,
        # so a Local can't be a non-Var for the first half of a block and
        # then a Var the second half, but then control returns to the first half
        method fresh_register($kind, $new = 0) {
            my @arr; my $type;
            # set $new to 1 here if you suspect a problem with the allocator,
            # or if you suspect a register is being double-released somewhere.
            # $new := 1;
               if $kind == $MVM_reg_int64 { @arr := @!ints; $type := int }
            elsif $kind == $MVM_reg_num64 { @arr := @!nums; $type := num }
            elsif $kind == $MVM_reg_str   { @arr := @!strs; $type := str }
            elsif $kind == $MVM_reg_obj   { @arr := @!objs; $type := NQPMu }
            else { nqp::die("unhandled reg kind $kind") }
            
            nqp::elems(@arr) && !$new ?? nqp::pop(@arr) !!
                    MAST::Local.new($!frame.add_local($type))
        }
        
        method release_i($reg) { self.release_register($reg, $MVM_reg_int64) }
        method release_n($reg) { self.release_register($reg, $MVM_reg_num64) }
        method release_s($reg) { self.release_register($reg, $MVM_reg_str) }
        method release_o($reg) { self.release_register($reg, $MVM_reg_obj) }
        
        method release_register($reg, $kind) {
            return 1 if $kind == $MVM_reg_void || $*BLOCK.is_var($reg);
            return nqp::push(@!ints, $reg) if $kind == $MVM_reg_int64;
            return nqp::push(@!nums, $reg) if $kind == $MVM_reg_num64;
            return nqp::push(@!strs, $reg) if $kind == $MVM_reg_str;
            return nqp::push(@!objs, $reg) if $kind == $MVM_reg_obj;
            nqp::die("unhandled reg kind $kind");
        }
    }
    
    # Holds information about the QAST::Block we're currently compiling.
    my class BlockInfo {
        has $!qast;                 # The QAST::Block
        has $!outer;                # Outer block's BlockInfo
        has %!local_names_by_index; # Locals' names by their indexes
        has %!locals;               # Mapping of local names to locals
        has %!local_kinds;          # Mapping of local registers to kinds
        has %!lexicals;             # Mapping of lexical names to lexicals
        has %!lexical_kinds;        # Mapping of lexical names to kinds
        has int $!param_idx;        # Current lexical parameter index
        has $!compiler;             # The QAST::MASTCompiler
        has @!params;               # List of QAST::Var param nodes
        has $!return_kind;          # kind of return, tracked while emitting
        
        method new($qast, $outer, $compiler) {
            my $obj := nqp::create(self);
            $obj.BUILD($qast, $outer, $compiler);
            $obj
        }
        
        method BUILD($qast, $outer, $compiler) {
            $!qast := $qast;
            $!outer := $outer;
            $!compiler := $compiler;
        }
        
        method add_param($var) {
            if $var.scope eq 'local' {
                self.register_local($var);
            }
            else {
                self.add_lexical($var)
            }
            @!params[+@!params] := $var;
        }
        
        method add_lexical($var) {
            my $type := $var.returns // NQPMu;
            my $kind := $!compiler.type_to_register_kind($type);
            my $index := $*MAST_FRAME.add_lexical($type, $var.name);
            self.register_lexical($var, $index, 0, $kind)
        }
        
        method register_lexical($var, $index, $outer, $kind) {
            my $name := $var.name;
            # not entirely sure whether this check should go here or in add_lexical
            if nqp::existskey(%!lexicals, $name) {
                nqp::die("Lexical '$name' already declared");
            }
            my $lex := MAST::Lexical.new( :index($index), :frames_out($outer) );
            %!lexicals{$name} := $lex;
            %!lexical_kinds{$name} := $kind;
            $lex;
        }
        
        method register_local($var) {
            my $name := $var.name;
            if nqp::existskey(%!locals, $name) {
                nqp::die("Local '$name' already declared");
            }
            my $kind := $!compiler.type_to_register_kind($var.returns // NQPMu);
            %!local_kinds{$name} := $kind;
            # pass a 1 meaning get a Totally New MAST::Local
            my $local := $*REGALLOC.fresh_register($kind, 1);
            %!locals{$name} := $local;
            %!local_names_by_index{$local.index} := $name;
            $local;
        }
        
        # returns whether a MAST::Local is a variable in this block
        method is_var($local) {
            nqp::existskey(%!local_names_by_index, $local.index)
        }
        
        method return_kind(*@value) {
            if @value {
                nqp::die("inconsistent immediate block return type")
                    if $!qast.blocktype eq 'immediate' &&
                        nqp::defined($!return_kind) && @value[0] != $!return_kind;
                $!return_kind := @value[0];
            }
            $!return_kind
        }
        
        method qast() { $!qast }
        method outer() { $!outer }
        method lexical($name) { %!lexicals{$name} }
        method lexicals() { %!lexicals }
        method local($name) { %!locals{$name} }
        method local_kind($name) { %!local_kinds{$name} }
        method lexical_kind($name) { %!lexical_kinds{$name} }
        method params() { @!params }
    }
    
    our $serno := 0;
    method unique($prefix = '') { $prefix ~ $serno++ }
    
    method to_mast($qast) {
        my $*MAST_COMPUNIT := MAST::CompUnit.new();
        
        # map a QAST::Block's cuid to the MAST::Frame we
        # created for it, so we can find the Frame later
        # when we encounter the Block again in a call.
        my %*MAST_FRAMES := nqp::hash();
        
        self.as_mast($qast);
        $*MAST_COMPUNIT
    }
    
    proto method as_mast($qast) { * }
    
    my @return_opnames := [
        'return',
        'return_i',
        'return_i',
        'return_i',
        'return_i',
        'return_n',
        'return_n',
        'return_s',
        'return_o'
    ];
    
    my @kind_to_op_slot := [
        0, 0, 0, 0, 0, 1, 1, 2, 3
    ];
    
    my @param_opnames := [
        'param_rp_i',
        'param_rp_n',
        'param_rp_s',
        'param_rp_o',
        'param_op_i',
        'param_op_n',
        'param_op_s',
        'param_op_o',
        'param_rn_i',
        'param_rn_n',
        'param_rn_s',
        'param_rn_o',
        'param_on_i',
        'param_on_n',
        'param_on_s',
        'param_on_o'
    ];
    
    my @return_types := [
        NQPMu,
        int,
        int,
        int,
        int,
        num,
        num,
        str,
        NQPMu
    ];
    
    multi method as_mast(QAST::Block $node) {
        my $outer_frame := try $*MAST_FRAME;
        
        # Create an empty frame and add it to the compilation unit.
        my $frame := MAST::Frame.new(
            :name(self.unique('frame_name_')),
            :cuuid(self.unique('frame_cuuid_')));
        
        $*MAST_COMPUNIT.add_frame($frame);
        my $outer    := try $*BLOCK;
        my $block    := BlockInfo.new($node, $outer, self);
        my $cuid     := $node.cuid();
        
        # stash the frame by the block's cuid so other references
        # by this block can find it.
        %*MAST_FRAMES{$cuid} := $frame;
        
        # set the outer if it exists
        $frame.set_outer($outer_frame)
            if $outer_frame && $outer_frame ~~ MAST::Frame;
        
        # Compile all the substatements.
        my $ins;
        {
            my $*BINDVAL := 0;
            
            # Create a register allocator for this frame.
            my $*REGALLOC := RegAlloc.new($frame);
        
            my $*BLOCK := $block;
            my $*MAST_FRAME := $frame;
            $ins := self.compile_all_the_stmts(@($node));
            
            # Add to instructions list for this block.
            nqp::splice($frame.instructions, $ins.instructions, 0, 0);
            
            $block.return_kind($ins.result_kind);
            # generate a return statement
            # get the return op name
            my $ret_op := @return_opnames[$ins.result_kind];
            my @ret_args := nqp::list();
            
            # provide the return arg register if needed
            nqp::push(@ret_args, $ins.result_reg) unless $ret_op eq 'return';
            
            # fixup the end of this frame's instruction list with the return
            push_op($frame.instructions, $ret_op, |@ret_args);
            
            # build up the frame prologue
            my @pre := nqp::list();
            my $min_args := 0;
            my $max_args := 0;
            my $param_index := 0;
            
            # build up instructions to bind the params
            for $block.params -> $var {
                
                $max_args++ unless $var.named;
                
                my $scope := $var.scope;
                nqp::die("Param scope must be 'local' or 'lexical'")
                    if $scope ne 'lexical' && $scope ne 'local';
                
                my $param_kind := self.type_to_register_kind($var.returns // NQPMu);
                my $opslot := @kind_to_op_slot[$param_kind];
                
                my $opname_index := ($var.named ?? 8 !! 0) + ($var.default ?? 4 !! 0) + $opslot;
                my $opname := @param_opnames[$opname_index];
                
                # what will be put in the value register
                my $val;
                
                if $var.named {
                    $val := MAST::SVal.new( :value($var.named) );
                }
                else { # positional
                    $min_args++;
                    $val := MAST::IVal.new( :size(16), :value($param_index));
                }
                
                # the variable register
                my $valreg := $scope eq 'lexical'
                    ?? $*REGALLOC.fresh_register($param_kind)
                    !! $block.local($var.name);
                
                # NQP->QAST always provides a default value for optional NQP params
                # even if no default initializer expression is provided.
                if $var.default {
                    # generate end label to skip initialization code
                    my $endlbl := MAST::Label.new( :name(self.unique('param') ~ '_end') );
                    
                    # generate default initialization code. Could also be
                    # wrapped in another QAST::Block.
                    my $default_mast := self.as_mast($var.default);
                    
                    nqp::die("default initialization result type doesn't match the param type")
                        unless $default_mast.result_kind == $param_kind;
                    
                    # emit param grabbing op
                    push_op(@pre, $opname, $valreg, $val, $endlbl);
                    
                    # emit default initialization code
                    push_ilist(@pre, $default_mast);
                    
                    # put the initialization result in the variable register
                    push_op(@pre, 'set', $valreg, $default_mast.result_reg);
                    
                    # end label to skip initialization code
                    nqp::push(@pre, $endlbl);
                }
                else {
                     # emit param grabbing op
                    push_op(@pre, $opname, $valreg, $val);
                }
                
                if $scope eq 'lexical' {
                    # emit the op to bind the lexical to the result register
                    push_op(@pre, 'bindlex', $block.lexical($var.name), $valreg);
                }
                $param_index++;
            }
            
            nqp::splice($frame.instructions, @pre, 0, 0);
            @pre := nqp::list();
            
            # XXX make $max_args MAX_UINT16 if there's a slurpy
            # check the arity 
            push_op(@pre, 'checkarity',
                MAST::IVal.new( :size(16), :value($min_args)),
                MAST::IVal.new( :size(16), :value($max_args)));
            nqp::splice($frame.instructions, @pre, 0, 0);
        }
        
        if $node.blocktype eq 'immediate' {
            my $call_mast := self.as_mast(
                QAST::Op.new( :op('call'),
                    :returns(@return_types[$block.return_kind]),
                    QAST::BVal.new( :value($node) ) ) );
            return $call_mast;
        }
        
        MAST::InstructionList.new(nqp::list(), MAST::VOID, $MVM_reg_void)
    }
    
    multi method as_mast(QAST::Stmts $node) {
        my $resultchild := $node.resultchild;
        nqp::die("resultchild out of range")
            if (nqp::defined($resultchild) && $resultchild >= +@($node));
        self.compile_all_the_stmts(@($node), $resultchild)
    }
    
    multi method as_mast(QAST::Stmt $node) {
        my $resultchild := $node.resultchild;
        nqp::die("resultchild out of range")
            if (nqp::defined($resultchild) && $resultchild >= +@($node));
        self.compile_all_the_stmts(@($node), $resultchild)
    }
    
    # This takes any node that is a statement list of some kind and compiles
    # all of the statements within it.
    method compile_all_the_stmts(@stmts, $resultchild?) {
        my @all_ins;
        my $last_stmt;
        my $result_stmt;
        my $result_count := 0;
        for @stmts {
            # Compile this child to MAST, and add its instructions to the end
            # of our instruction list. Also track the last statement.
            $last_stmt := self.as_mast($_);
            nqp::splice(@all_ins, $last_stmt.instructions, +@all_ins, 0);
            $result_stmt := $last_stmt
                if !nqp::defined($resultchild) ||
                    nqp::defined($resultchild) && $result_count == $resultchild;
            $result_count++;
        }
        if $result_stmt {
            MAST::InstructionList.new(@all_ins, $result_stmt.result_reg, $result_stmt.result_kind);
        }
        else {
            MAST::InstructionList.new(@all_ins, MAST::VOID, $MVM_reg_void);
        }
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
    
    sub check_kinds($a, $b) {
        nqp::die("register types don't match") unless $a == $b;
    }
    
    my @getlex_n_opnames := [
        'getlex_ni',
        'getlex_nn',
        'getlex_ns',
        'getlex_no'
    ];
    
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
                    $*BLOCK.register_local($node);
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
        my $res_kind;
        if $scope eq 'local' {
            if $*BLOCK.local($name) -> $local {
                $res_kind := $*BLOCK.local_kind($name);
                if $*BINDVAL {
                    my $valmast := self.as_mast_clear_bindval($*BINDVAL);
                    check_kinds($valmast.result_kind, $res_kind);
                    push_ilist(@ins, $valmast);
                    push_op(@ins, 'set', $local, $valmast.result_reg);
                    $*REGALLOC.release_register($valmast.result_reg, $res_kind);
                }
                $res_reg := $local;
            }
            else {
                nqp::die("Cannot reference undeclared local '$name'");
            }
        }
        elsif $scope eq 'lexical' {
            my $lex;
            my $outer := 0;
            my $block := $*BLOCK;
            # find the block where the lexical was declared, if any
            while !($lex := $block.lexical($name)) && ($block := $block.outer) ~~ BlockInfo {
                $outer++;
            }
            if $lex {
                $res_kind := $block.lexical_kind($name);
                if $outer {
                    # cache the lexical in this block, also to prevent it being declared
                    $lex := $*BLOCK.register_lexical($node, $lex.index, $outer, $res_kind);
                }
                if $*BINDVAL {
                    my $valmast := self.as_mast_clear_bindval($*BINDVAL);
                    check_kinds($valmast.result_kind, $res_kind);
                    $res_reg := $valmast.result_reg;
                    push_ilist(@ins, $valmast);
                    push_op(@ins, 'bindlex', $lex, $res_reg);
                    $*REGALLOC.release_register($res_reg, $valmast.result_kind);
                }
                else {
                    $res_reg := $*REGALLOC.fresh_register($res_kind);
                    push_op(@ins, 'getlex', $res_reg, $lex);
                }
            }
            else {
                nqp::die("Missing lexical $name at least needs to know what type it should be")
                    unless $node.returns;
                $res_kind := self.type_to_register_kind($node.returns);
                $res_reg := $*REGALLOC.fresh_register($res_kind);
                push_op(@ins, @getlex_n_opnames[@kind_to_op_slot[$res_kind]],
                    $res_reg, MAST::SVal.new( :value($name) ));
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
            my $type    := self.type_to_register_kind($node.returns);
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
        
        MAST::InstructionList.new(@ins, $res_reg, $res_kind)
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

    multi method as_mast(QAST::BVal $bv) {
        
        my $block := $bv.value;
        my $cuid  := $block.cuid();
        my $frame := %*MAST_FRAMES{$cuid};
        nqp::die("QAST::Block with cuid $cuid has not appeared")
            unless $frame && $frame ~~ MAST::Frame;
        
        my $reg := $*REGALLOC.fresh_o();
        MAST::InstructionList.new(
            [MAST::Op.new(
                :bank('primitives'), :op('getcode'),
                $reg,
                $frame
            )],
            $reg,
            $MVM_reg_obj)
    }

    multi method as_mast(QAST::Regex $node) {
        # Prefix for the regexes code pieces.
        my $prefix := self.unique('rx') ~ '_';

        # Build the list of (unique) registers we need
        my %*REG := nqp::hash(
            'tgt', $*REGALLOC.fresh_s(),
            'pos', $*REGALLOC.fresh_i(),
            'off', $*REGALLOC.fresh_i(),
            'eos', $*REGALLOC.fresh_i(),
            'rep', $*REGALLOC.fresh_i(),
            'cur', $*REGALLOC.fresh_o(),
            'curclass', $*REGALLOC.fresh_o(),
            'bstack', $*REGALLOC.fresh_o(),
            'cstack', $*REGALLOC.fresh_o());

        # create our labels
        my $startlabel   := MAST::Label.new( :name($prefix ~ 'start') );
        my $donelabel    := MAST::Label.new( :name($prefix ~ 'done') );
        my $restartlabel := MAST::Label.new( :name($prefix ~ 'restart') );
        my $faillabel    := MAST::Label.new( :name($prefix ~ 'fail') );
        my $jumplabel    := MAST::Label.new( :name($prefix ~ 'jump') );
        my $cutlabel     := MAST::Label.new( :name($prefix ~ 'cut') );
        my $cstacklabel  := MAST::Label.new( :name($prefix ~ 'cstack_done') );
        %*REG<fail>      := $faillabel;

        nqp::die("Regex compilation NYI");
    }
    
    my @prim_to_reg := [$MVM_reg_obj, $MVM_reg_int64, $MVM_reg_num64, $MVM_reg_str];
    method type_to_register_kind($type) {
        @prim_to_reg[pir::repr_get_primitive_type_spec__IP($type)]
    }
}

sub push_op(@dest, $op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);
    
    nqp::push(@dest, MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    ));
}
