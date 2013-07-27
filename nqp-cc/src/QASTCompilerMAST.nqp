use QASTOperationsMAST;
use QASTRegexCompilerMAST;

# Disable compilatin of deserialization stuff while still in development.
my $ENABLE_SC_COMP := 1;

my $MVM_reg_void            := 0; # not really a register; just a result/return kind marker
my $MVM_reg_int8            := 1;
my $MVM_reg_int16           := 2;
my $MVM_reg_int32           := 3;
my $MVM_reg_int64           := 4;
my $MVM_reg_num32           := 5;
my $MVM_reg_num64           := 6;
my $MVM_reg_str             := 7;
my $MVM_reg_obj             := 8;

# Mapping of QAST::Want type identifiers to return types.
my %WANTMAP := nqp::hash(
    'v', $MVM_reg_void,
    'I', $MVM_reg_int64, 'i', $MVM_reg_int64,
    'N', $MVM_reg_num64, 'n', $MVM_reg_num64,
    'S', $MVM_reg_str, 's', $MVM_reg_str,
    'P', $MVM_reg_obj, 'p', $MVM_reg_obj
);

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
        has %!released_indexes;

        method new($frame) {
            my $obj := nqp::create(self);
            nqp::bindattr($obj, RegAlloc, '$!frame', $frame);
            nqp::bindattr($obj, RegAlloc, '@!objs', []);
            nqp::bindattr($obj, RegAlloc, '@!ints', []);
            nqp::bindattr($obj, RegAlloc, '@!nums', []);
            nqp::bindattr($obj, RegAlloc, '@!strs', []);
            nqp::bindattr($obj, RegAlloc, '%!released_indexes', {});
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

            my $reg;
            if nqp::elems(@arr) && !$new {
                $reg := nqp::pop(@arr);
                nqp::deletekey(%!released_indexes, $reg.index);
            }
            else {
                $reg := MAST::Local.new($!frame.add_local($type));
            }
            $reg
        }

        method release_i($reg) { self.release_register($reg, $MVM_reg_int64) }
        method release_n($reg) { self.release_register($reg, $MVM_reg_num64) }
        method release_s($reg) { self.release_register($reg, $MVM_reg_str) }
        method release_o($reg) { self.release_register($reg, $MVM_reg_obj) }

        method release_register($reg, $kind, $force = 0) {
            return 1 if $kind == $MVM_reg_void || !$force && $*BLOCK.is_var($reg)
                || nqp::existskey(%!released_indexes, $reg.index);
            %!released_indexes{$reg.index} := 1;
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
        has %!lexical_params;       # Mapping of lexical param names to their initial result reg
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
            %!local_names_by_index := nqp::hash();
            %!locals := nqp::hash();
            %!local_kinds := nqp::hash();
            %!lexicals := nqp::hash();
            %!lexical_kinds := nqp::hash();
            %!lexical_params := nqp::hash();
            @!params := nqp::list()
        }

        method add_param($var) {
            @!params[+@!params] := $var;
            if $var.scope eq 'local' {
                self.register_local($var);
            }
            else {
                my $res_kind := self.add_lexical($var);
                my $res_reg := $*REGALLOC.fresh_register($res_kind);
                %!lexical_params{$var.name} := $res_reg;
                [$res_kind, $res_reg]
            }
        }

        method add_lexical($var, :$is_static, :$is_cont, :$is_state) {
            my $type := $var.returns;
            my $kind := $!compiler.type_to_register_kind($type);
            my $index := $*MAST_FRAME.add_lexical($type, $var.name);
            self.register_lexical($var, $index, 0, $kind);
            if $is_static || $is_cont || $is_state {
                my %blv := %*BLOCK_LEX_VALUES;
                unless nqp::existskey(%blv, $!qast.cuid) {
                    %blv{$!qast.cuid} := [];
                }
                my $flags := $is_static ?? 0 !!
                             $is_cont   ?? 1 !! 2;
                nqp::push(%blv{$!qast.cuid}, [$var.name, $var.value, $flags]);
            }
            $kind;
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
            my $temporary := ?$*INSTMT;
            if nqp::existskey(%!locals, $name) ||
                    $temporary && nqp::existskey(%*STMTTEMPS, $name) {
                nqp::die("Local '$name' already declared");
            }
            my $kind := $!compiler.type_to_register_kind($var.returns);
            %!local_kinds{$name} := $kind;
            # pass a 1 meaning get a Totally New MAST::Local
            my $local := $*REGALLOC.fresh_register($kind, !$temporary);
            %!locals{$name} := $local;
            %!local_names_by_index{$local.index} := $name;
            if $temporary {
                %*STMTTEMPS{$name} := $local;
            }
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

        method release_temp($name) {
            my $local := %!locals{$name};
            my $index := $local.index();
            my $kind := %!local_kinds{$name};
            $*REGALLOC.release_register($local, $kind, 1);
            nqp::deletekey(%!local_names_by_index, $index);
            nqp::deletekey(%!locals, $name);
            nqp::deletekey(%!local_kinds, $name);
        }

        method qast() { $!qast }
        method outer() { $!outer }
        method lexical($name) { %!lexicals{$name} }
        method lexicals() { %!lexicals }
        method local($name) { %!locals{$name} }
        method local_kind($name) { %!local_kinds{$name} }
        method lexical_kind($name) { %!lexical_kinds{$name} }
        method lexical_kinds() { %!lexical_kinds }
        method params() { @!params }
        method lexical_param($name) { %!lexical_params{$name} }

        method resolve_lexical($name) {
            my $block := self;
            my $out := 0;
            while $block {
                my $lex := ($block.lexicals()){$name};
                return MAST::Lexical.new( :index($lex.index), :frames_out($out) ) if $lex;
                $out++;
                $block := $block.outer;
            }
            nqp::die("could not resolve lexical $name");
        }
    }

    our $serno := 0;
    method unique($prefix = '') { $prefix ~ $serno++ }

    method to_mast($qast) {
        my $*MAST_COMPUNIT := MAST::CompUnit.new();

        # map a QAST::Block's cuid to the MAST::Frame we
        # created for it, so we can find the Frame later
        # when we encounter the Block again in a call.
        my %*MAST_FRAMES := nqp::hash();

        my $*QASTCOMPILER := self;
        self.as_mast($qast);

        $*MAST_COMPUNIT
    }

    method coerce($res, $desired) {
        my $got := $res.result_kind;
        if $got != $desired {
            $res.append(self.coercion($res, $desired));
        }
        $res
    }

    # Expects that the value in need of coercing has already been
    # obtained. Produces instructions to coerce it.
    method coercion($res, $desired) {
        my $il := nqp::list();
        my $got := $res.result_kind;
        my $reg := $res.result_reg;
        if $got == $desired {
            # Nothing to do.
        }
        elsif $desired == $MVM_reg_void {
            $reg := MAST::VOID;
            $got := $MVM_reg_void;
        }
        elsif $desired == $MVM_reg_obj {
            my $hll := '';
            try $hll := $*HLL;
            return QAST::MASTOperations.box(self, $hll, $got, $reg);
        }
        elsif $got == $MVM_reg_obj {
            my $hll := '';
            try $hll := $*HLL;
            return QAST::MASTOperations.unbox(self, $hll, $desired, $reg);
        }
        else {
            my $res_reg := $*REGALLOC.fresh_register($desired);
            if $desired == $MVM_reg_int64 {
                if $got == $MVM_reg_num64 {
                    push_op($il, 'coerce_ni', $res_reg, $reg);
                }
                elsif $got == $MVM_reg_str {
                    push_op($il, 'coerce_si', $res_reg, $reg);
                }
                else {
                    nqp::die("Unknown coercion case for int");
                }
            }
            elsif $desired == $MVM_reg_num64 {
                if $got == $MVM_reg_int64 {
                    push_op($il, 'coerce_in', $res_reg, $reg);
                }
                elsif $got == $MVM_reg_str {
                    push_op($il, 'coerce_sn', $res_reg, $reg);
                }
                else {
                    nqp::die("Unknown coercion case for num");
                }
            }
            elsif $desired == $MVM_reg_str {
                if $got == $MVM_reg_int64 {
                    push_op($il, 'coerce_is', $res_reg, $reg);
                }
                elsif $got == $MVM_reg_num64 {
                    push_op($il, 'coerce_ns', $res_reg, $reg);
                }
                else {
                    nqp::die("Unknown coercion case for str");
                }
            }
            else {
                nqp::die("Coercion from type '$got' to '$desired' NYI");
            }
            $*REGALLOC.release_register($reg, $got);
            $reg := $res_reg;
        }
        MAST::InstructionList.new($il, $reg, $desired)
    }

    proto method as_mast($qast, :$want) {
        my $*WANT;
        if nqp::defined($want) {
            $*WANT := %WANTMAP{$want} // $want;
            if nqp::istype($qast, QAST::Want) {
                self.coerce(self.as_mast(want($qast, $*WANT)), $*WANT)
            }
            else {
                self.coerce({*}, $*WANT)
            }
        }
        else {
            {*}
        }
    }

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

    my @type_initials := [
        '', 'i', 'i', 'i', 'i', 'n', 'n', 's', 'o'
    ];

    my @attr_opnames := [
        '',
        'attr_i',
        'attr_i',
        'attr_i',
        'attr_i',
        'attr_n',
        'attr_n',
        'attr_s',
        'attr_o'
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

    my @return_types := [ NQPMu, int, int, int, int, num, num, str, NQPMu ];

    method register_kind_to_type($kind) { @return_types[$kind] }

    multi method as_mast(QAST::CompUnit $cu, :$want) {
        # Set HLL.
        my $*HLL := '';
        if $cu.hll {
            $*HLL := $cu.hll;
            $*MAST_COMPUNIT.hll($*HLL);
        }
        if (nqp::getenvhash()<MVMCCDEBUG>) { say($cu.dump); }

        # Should have a single child which is the outer block.
        if +@($cu) != 1 || !nqp::istype($cu[0], QAST::Block) {
            nqp::die("QAST::CompUnit should have one child that is a QAST::Block");
        }

        # Hash mapping blocks with static lexicals to an array of arrays. Each
        # of the sub-arrays has the form [$name, $value, $flags], where flags
        # are 0 = static lex, 1 = container, 2 = state container.
        my %*BLOCK_LEX_VALUES;

        # Compile the block.
        self.as_mast($cu[0]);

        # If we are in compilation mode, or have pre-deserialization or
        # post-deserialization tasks, handle those. Overall, the process
        # is to desugar this into simpler QAST nodes, then compile those.
        my $comp_mode := $cu.compilation_mode;
        my @pre_des   := $cu.pre_deserialize;
        my @post_des  := $cu.post_deserialize;
        if %*BLOCK_LEX_VALUES {
            nqp::push(@post_des, QAST::Op.new( :op('setup_blv'), %*BLOCK_LEX_VALUES ));
        }
        if $ENABLE_SC_COMP && ($comp_mode || @pre_des || @post_des) {
            # Create a block into which we'll install all of the other
            # pieces.
            my $block := QAST::Block.new( :blocktype('raw') );

            # Add pre-deserialization tasks, each as a QAST::Stmt.
            for @pre_des {
                $block.push(QAST::Stmt.new($_));
            }

            # If we need to do deserialization, emit code for that.
            if $comp_mode {
                $block.push(self.deserialization_code($cu.sc(), $cu.code_ref_blocks(),
                    $cu.repo_conflict_resolver()));
            }

            # Add post-deserialization tasks.
            for @post_des {
                $block.push(QAST::Stmt.new($_));
            }

            # Compile to MAST and register this block as the deserialization
            # handler.
            self.as_mast($block);
            $*MAST_COMPUNIT.deserialize_frame(%*MAST_FRAMES{$block.cuid});
        }

        # Compile and include load-time logic, if any.
        if nqp::defined($cu.load) {
            my $load_block := QAST::Block.new(
                :blocktype('raw'),
                $cu.load
            );
            self.as_mast($load_block);
            $*MAST_COMPUNIT.load_frame(%*MAST_FRAMES{$load_block.cuid});
        }

        # Compile and include main-time logic, if any, and then add a Java
        # main that will lead to its invocation.
        if nqp::defined($cu.main) {
            my $main_block := QAST::Block.new(
                :blocktype('raw'),
                $cu.main
            );
            self.as_mast($main_block);
            $*MAST_COMPUNIT.main_frame(%*MAST_FRAMES{$main_block.cuid});
        }
    }

    method deserialization_code($sc, @code_ref_blocks, $repo_conf_res) {
        # Serialize it.
        my $sh := nqp::list_s();
        my $serialized := nqp::serialize($sc, $sh);

        # Now it's serialized, pop this SC off the compiling SC stack.
        nqp::popcompsc();

        # String heap QAST.
        my $sh_ast := QAST::Op.new( :op('list_s') );
        my $sh_elems := nqp::elems($sh);
        my $i := 0;
        while $i < $sh_elems {
            $sh_ast.push(nqp::isnull_s($sh[$i])
                ?? QAST::Op.new( :op('null_s') )
                !! QAST::SVal.new( :value($sh[$i]) ));
            $i := $i + 1;
        }

        # Code references.
        my $cr_past := QAST::Op.new( :op('list_b'), |@code_ref_blocks );

        # Handle repossession conflict resolution code, if any.
        if $repo_conf_res {
            $repo_conf_res.push(QAST::Var.new( :name('conflicts'), :scope('local') ));
        }
        else {
            $repo_conf_res := QAST::Op.new(
                :op('die_s'),
                QAST::SVal.new( :value('Repossession conflicts occurred during deserialization') )
            );
        }

        # Overall deserialization QAST.
        QAST::Stmts.new(
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('cur_sc'), :scope('local'), :decl('var') ),
                QAST::Op.new( :op('createsc'), QAST::SVal.new( :value($sc.handle()) ) )
            ),
            QAST::Op.new(
                :op('scsetdesc'),
                QAST::Var.new( :name('cur_sc'), :scope('local') ),
                QAST::SVal.new( :value($sc.description) )
            ),
            QAST::Op.new(
                :op('bind'),
                QAST::Var.new( :name('conflicts'), :scope('local'), :decl('var') ),
                QAST::Op.new( :op('list') )
            ),
            QAST::Op.new(
                :op('deserialize'),
                QAST::SVal.new( :value($serialized) ),
                QAST::Var.new( :name('cur_sc'), :scope('local') ),
                $sh_ast,
                QAST::Block.new( :blocktype('immediate'), $cr_past ),
                QAST::Var.new( :name('conflicts'), :scope('local') )
            ),
            QAST::Op.new(
                :op('if'),
                QAST::Op.new(
                    :op('elems'),
                    QAST::Var.new( :name('conflicts'), :scope('local') )
                ),
                $repo_conf_res
            )
        )
    }

    multi method as_mast(QAST::Block $node, :$want) {
        my $outer_frame := try $*MAST_FRAME;

        # Create an empty frame and add it to the compilation unit.
        my $frame := MAST::Frame.new(
            :name($node.name || self.unique('frame_name_')),
            :cuuid(self.unique('frame_cuuid_')));

        $*MAST_COMPUNIT.add_frame($frame);
        my $outer;
        try $outer   := $*BLOCK;
        my $block    := BlockInfo.new($node, (nqp::defined($outer) ?? $outer !! NQPMu), self);
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

            # when we enter a QAST::Stmt, the contextual will be cloned, and the locals of
            # newly declared QAST::Vars of local scope inside the Stmt will be stashed here,
            # so they can be released at the end of the QAST::Stmt in which they were
            # declared.  Inability to declare duplicate names is still enfoced, and types are
            # still enforced.
            my %*STMTTEMPS := nqp::hash();
            my $*INSTMT := 0;

            my $*BLOCK := $block;
            my $*MAST_FRAME := $frame;

            $ins := self.compile_all_the_stmts(@($node));

            # Add to instructions list for this block.
            nqp::splice($frame.instructions, $ins.instructions, +$frame.instructions, 0);

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
            my $param_index := 0;

            # Analyze parameters to get count of required/optional and make sure
            # all is in order.
            my int $pos_required := 0;
            my int $pos_optional := 0;
            my int $pos_slurpy   := 0;
            for $block.params {
                if $_.named {
                    # Don't count.
                }
                elsif $_.slurpy {
                    if $pos_slurpy {
                        nqp::die("Only one slurpy positional allowed");
                    }
                    $pos_slurpy := 1;
                }
                elsif $_.default {
                    if $pos_slurpy {
                        nqp::die("Optional positionals must come before all slurpy positionals");
                    }
                    $pos_optional++;
                }
                else {
                    if $pos_optional {
                        nqp::die("Required positionals must come before all optional positionals");
                    }
                    if $pos_slurpy {
                        nqp::die("Required positionals must come before all slurpy positionals");
                    }
                    $pos_required++;
                }
            }

            # check the arity
            push_op(@pre, 'checkarity',
                MAST::IVal.new( :size(16), :value($pos_required)),
                MAST::IVal.new( :size(16), :value($pos_slurpy ?? -1 !! $pos_required + $pos_optional)));

            # build up instructions to bind the params
            for $block.params -> $var {

                my $scope := $var.scope;
                nqp::die("Param scope must be 'local' or 'lexical'")
                    if $scope ne 'lexical' && $scope ne 'local';

                my $param_kind := self.type_to_register_kind($var.returns // NQPMu);
                my $opslot := @kind_to_op_slot[$param_kind];

                my $opname_index := ($var.named ?? 8 !! 0) + ($var.default ?? 4 !! 0) + $opslot;
                my $opname := @param_opnames[$opname_index];

                # what will be put in the value register
                my $val;

                if $var.slurpy {
                    if $var.named {
                        $opname := "param_sn";
                    }
                    else {
                        $opname := "param_sp";
                    }
                }
                elsif $var.named {
                    $val := MAST::SVal.new( :value($var.named) );
                }
                else { # positional
                    $val := MAST::IVal.new( :size(16), :value($param_index));
                }

                # the variable register
                my $valreg := $scope eq 'lexical'
                    ?? $block.lexical_param($var.name)
                    !! $block.local($var.name);

                # NQP->QAST always provides a default value for optional NQP params
                # even if no default initializer expression is provided.
                if $var.default {
                    # generate end label to skip initialization code
                    my $endlbl := MAST::Label.new( :name(self.unique('param') ~ '_end') );

                    # generate default initialization code. Could also be
                    # wrapped in another QAST::Block.
                    my $default_mast := self.as_mast($var.default, :want($param_kind));

                #    nqp::die("default initialization result type doesn't match the param type")
                #        unless $default_mast.result_kind == $param_kind;

                    # emit param grabbing op
                    push_op(@pre, $opname, $valreg, $val, $endlbl);

                    # emit default initialization code
                    push_ilist(@pre, $default_mast);

                    # put the initialization result in the variable register
                    push_op(@pre, 'set', $valreg, $default_mast.result_reg);
                    $*REGALLOC.release_register($default_mast.result_reg, $default_mast.result_kind);

                    # end label to skip initialization code
                    nqp::push(@pre, $endlbl);
                }
                elsif $var.slurpy {
                    if $var.named {
                        push_op(@pre, $opname, $valreg);
                    }
                    else {
                        push_op(@pre, $opname, $valreg, MAST::IVal.new( :value($pos_required + $pos_optional) ));
                    }
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
        }

        if $node.blocktype eq 'immediate' {
            return self.as_mast(
                QAST::Op.new( :op('call'),
                    :returns(@return_types[$block.return_kind]),
                    QAST::BVal.new( :value($node) ) ), :want($block.return_kind) );
        }
        elsif $node.blocktype eq 'raw' {
            return MAST::InstructionList.new(nqp::list(), MAST::VOID, $MVM_reg_void);
        }
        elsif $node.blocktype && $node.blocktype ne 'declaration' {
            nqp::die("Unhandled blocktype $node.blocktype");
        }
        # note: we're now in the outer $*BLOCK/etc. contexts
        # blocktype declaration (default)
        if $outer && $outer ~~ BlockInfo {
            self.as_mast(QAST::BVal.new(:value($node)))
        }
        else {
            MAST::InstructionList.new(nqp::list(), MAST::VOID, $MVM_reg_void)
        }
    }

    multi method as_mast(QAST::Stmts $node, :$want) {
        my $resultchild := $node.resultchild;
        nqp::die("resultchild out of range")
            if (nqp::defined($resultchild) && $resultchild >= +@($node));
        self.compile_all_the_stmts(@($node), $resultchild)
    }

    multi method as_mast(QAST::Stmt $node, :$want) {
        my $resultchild := $node.resultchild;
        my %stmt_temps := nqp::clone(%*STMTTEMPS); # guaranteed to be initialized
        my $result;
        {
            my %*STMTTEMPS := %stmt_temps;
            my $*INSTMT := 1;

            nqp::die("resultchild out of range")
                if (nqp::defined($resultchild) && $resultchild >= +@($node));
            $result := self.compile_all_the_stmts(@($node), $resultchild);
        }
        for %stmt_temps -> $temp_key {
            if !nqp::existskey(%*STMTTEMPS, $temp_key) {
                $*BLOCK.release_temp($temp_key);
            }
        }
        $result
    }

    # This takes any node that is a statement list of some kind and compiles
    # all of the statements within it.
    method compile_all_the_stmts(@stmts, $resultchild?, :$want) {
        my @all_ins;
        my $last_stmt;
        my $result_stmt;
        my $result_count := 0;
        $resultchild := $resultchild // -1;
        my $last_stmt_num := +@stmts - 1;
        for @stmts {
            # Compile this child to MAST, and add its instructions to the end
            # of our instruction list. Also track the last statement.
            if $result_count == $resultchild || $resultchild == -1
                    && $result_count == $last_stmt_num
                    && nqp::defined($want) {
                $last_stmt := self.as_mast($_, :want($want));
            }
            else {
                $last_stmt := self.as_mast($_);
            }
            nqp::splice(@all_ins, $last_stmt.instructions, +@all_ins, 0);
            if $result_count == $resultchild || $resultchild == -1 && $result_count == $last_stmt_num {
                $result_stmt := $last_stmt;
            }
            else { # release top-level results (since they can't be used by anything anyway)
                $*REGALLOC.release_register($last_stmt.result_reg, $last_stmt.result_kind);
            }
            $result_count++;
        }
        if $result_stmt && $result_stmt.result_kind != $MVM_reg_void {
            MAST::InstructionList.new(@all_ins, $result_stmt.result_reg, $result_stmt.result_kind);
        }
        else {
            MAST::InstructionList.new(@all_ins, $*REGALLOC.fresh_o(), $MVM_reg_obj);
        }
    }

    multi method as_mast(QAST::Op $node, :$want) {
        QAST::MASTOperations.compile_op(self, $*HLL, $node)
    }

    multi method as_mast(QAST::VM $node, :$want) {
        if $node.supports('moar') {
            return nqp::defined($want)
                ?? self.as_mast($node.alternative('moar'), :$want)
                !! self.as_mast($node.alternative('moar'));
        }
        elsif $node.supports('moarop') {
            return nqp::defined($want)
                ?? QAST::MASTOperations.compile_mastop(self, $node.alternative('moarop'), $node.list, :$want)
                !! QAST::MASTOperations.compile_mastop(self, $node.alternative('moarop'), $node.list);
        }
        elsif $node.supports('mast') {
            return $node.alternative('mast');
        }
        elsif $node.supports('jvm') {
            # Currently, NQP spits out very generic code in the JVM option, so we
            # will pretend we are a JVM for now. This means we should be able to
            # get a fairly long way on a stock NQP, rather than needing secret
            # hacks. :-)
            return nqp::defined($want)
                ?? self.as_mast($node.alternative('jvm'), :$want)
                !! self.as_mast($node.alternative('jvm'));
        }
        else {
            nqp::die("To compile on the MoarVM backend, QAST::VM must have an alternative 'moar' or 'moarop'");
        }
    }

    sub check_kinds($a, $b) {
        nqp::die("register types $a and $b don't match") unless $a == $b;
    }

    my @lex_n_opnames := [
        'lex_ni',
        'lex_nn',
        'lex_ns',
        'lex_no'
    ];

    multi method as_mast(QAST::Var $node, :$want) {
        self.compile_var($node)
    }

    multi method as_mast(QAST::VarWithFallback $node, :$want) {
        my $var_res := self.compile_var($node);
        if $*BINDVAL || $var_res.result_kind != $MVM_reg_obj {
            $var_res
        }
        else {
            my $il := nqp::list();
            push_ilist($il, $var_res);

            my $fallback_if_nonnull := MAST::Label.new(:name($node.unique('fallback_if_nonnull')));
            my $fallback_end := MAST::Label.new(:name($node.unique('fallback_end')));
            my $res_reg := $*REGALLOC.fresh_o();
            push_op($il, 'ifnonnull', $var_res.result_reg, $fallback_if_nonnull);

            my $fallback_res := self.as_mast($node.fallback, :want($MVM_reg_obj));
            push_ilist($il, $fallback_res);
            push_op($il, 'set', $res_reg, $fallback_res.result_reg);
            push_op($il, 'goto', $fallback_end);
            nqp::push($il, $fallback_if_nonnull);
            push_op($il, 'set', $res_reg, $var_res.result_reg);
            nqp::push($il, $fallback_end);
            $*REGALLOC.release_register($var_res.result_reg, $MVM_reg_obj);
            $*REGALLOC.release_register($fallback_res.result_reg, $MVM_reg_obj);

            MAST::InstructionList.new($il, $res_reg, $MVM_reg_obj)
        }
    }

    method compile_var($node, :$want) {
        my $scope := $node.scope;
        my $decl  := $node.decl;

        my $res_reg;
        my $res_kind;

        # Handle any declarations; after this, we call through to the
        # lookup code.
        if $decl {
            # If it's a parameter, add it to the things we should bind
            # at block entry.
            if $decl eq 'param' {
                if $scope eq 'local' {
                    $*BLOCK.add_param($node);
                }
                elsif $scope eq 'lexical' {
                    my @details := $*BLOCK.add_param($node);
                    $res_kind := @details[0];
                    $res_reg := @details[1];
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
            elsif $decl eq 'static' {
                if $scope ne 'lexical' {
                    nqp::die("Can only use 'static' decl with scope 'lexical'");
                }
                $*BLOCK.add_lexical($node, :is_static);
            }
            elsif $decl eq 'contvar' {
                if $scope ne 'lexical' {
                    nqp::die("Can only use 'contvar' decl with scope 'lexical'");
                }
                $*BLOCK.add_lexical($node, :is_cont);
            }
            elsif $decl eq 'statevar' {
                if $scope ne 'lexical' {
                    nqp::die("Can only use 'statevar' decl with scope 'lexical'");
                }
                $*BLOCK.add_lexical($node, :is_state);
            }
            else {
                nqp::die("Don't understand declaration type '$decl'");
            }
        }

        # Now go by scope.
        my $name := $node.name;
        my @ins;
        if $scope eq 'local' {
            if $*BLOCK.local($name) -> $local {
                $res_kind := $*BLOCK.local_kind($name);
                if $*BINDVAL {
                    my $valmast := self.as_mast_clear_bindval($*BINDVAL, :want($res_kind));
                #    check_kinds($valmast.result_kind, $res_kind);
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
                    # need to create lex that knows how many frames to go out
                    $lex := MAST::Lexical.new( :index($lex.index), :frames_out($outer) );
                }
                if $*BINDVAL {
                    my $valmast := self.as_mast_clear_bindval($*BINDVAL, :want($res_kind));
                #    check_kinds($valmast.result_kind, $res_kind);
                    $res_reg := $valmast.result_reg;
                    push_ilist(@ins, $valmast);
                    push_op(@ins, 'bindlex', $lex, $res_reg);
                    # do NOT release your own result register.  ergh.
                    #$*REGALLOC.release_register($res_reg, $valmast.result_kind);
                }
                elsif $decl ne 'param' {
                    $res_reg := $*REGALLOC.fresh_register($res_kind);
                    push_op(@ins, 'getlex', $res_reg, $lex);
                }
                else {
                    # for lexical param declarations, we don't actually have a result value,
                    # since the param bindlex may be stale by the time the result register
                    # could be used, since the bindlex always occurs at the very top,
                    # so turn around and release the temp register already preallocated.
                    $*REGALLOC.release_register($res_reg, $res_kind);
                    # get another one in case someone is using it...
                    $res_reg := $*REGALLOC.fresh_register($res_kind);
                    push_op(@ins, 'getlex', $res_reg, $lex);
                }
            }
            else {
                if $*BINDVAL {
                    my $valmast := $node.returns
                        ?? self.as_mast_clear_bindval($*BINDVAL, :want(self.type_to_register_kind($node.returns)))
                        !! self.as_mast_clear_bindval($*BINDVAL);
                    $res_reg := $valmast.result_reg;
                    push_ilist(@ins, $valmast);
                    push_op(@ins, "bind"~@lex_n_opnames[@kind_to_op_slot[$res_kind]], MAST::SVal.new( :value($name) ), $res_reg);
                    $res_kind := $valmast.result_kind;
                }
                else {
                    $res_kind := $node.returns ?? self.type_to_register_kind($node.returns) !!  $MVM_reg_obj;
                    $res_reg := $*REGALLOC.fresh_register($res_kind);
                    push_op(@ins, "get"~@lex_n_opnames[@kind_to_op_slot[$res_kind]],
                        $res_reg, MAST::SVal.new( :value($name) ));
                }
            }
        }
        elsif $scope eq 'contextual' {
            my $name_const := const_s($name);
            if $*BINDVAL {
                my $valmast := self.as_mast_clear_bindval($*BINDVAL, :want($MVM_reg_obj));
            #    check_kinds($valmast.result_kind, $MVM_reg_obj);
                $res_reg := $valmast.result_reg;
                push_ilist(@ins, $valmast);
                push_ilist(@ins, $name_const);
                push_op(@ins, 'binddynlex', $name_const.result_reg, $res_reg);
                # do NOT release your own result register.  ergh.
                #$*REGALLOC.release_register($res_reg, $valmast.result_kind);
            }
            else {
                push_ilist(@ins, $name_const);
                $res_reg := $*REGALLOC.fresh_register($MVM_reg_obj);
                push_op(@ins, 'getdynlex', $res_reg, $name_const.result_reg);
            }
            $*REGALLOC.release_register($name_const.result_reg, $MVM_reg_str);
            $res_kind := $MVM_reg_obj;
        }
        elsif $scope eq 'attribute' {
            # Ensure we have object and class handle.
            my @args := $node.list();
            if +@args != 2 {
                nqp::die("An attribute lookup needs an object and a class handle");
            }

            # Compile object and handle.
            my $obj := self.as_mast_clear_bindval(@args[0], :want($MVM_reg_obj));
            my $han := self.as_mast_clear_bindval(@args[1], :want($MVM_reg_obj));
            push_ilist(@ins, $obj);
            push_ilist(@ins, $han);

            # Go by whether it's a bind or lookup.
            my $kind := self.type_to_register_kind($node.returns);
            if $*BINDVAL {
                my $valmast := self.as_mast_clear_bindval($*BINDVAL, :want($kind));

                push_ilist(@ins, $valmast);
                push_op(@ins, 'bind' ~ @attr_opnames[$kind], $obj.result_reg,
                    $han.result_reg, MAST::SVal.new( :value($name) ), $valmast.result_reg,
                        MAST::IVal.new( :value(-1) ) );
                $res_reg := $valmast.result_reg;
                $res_kind := $valmast.result_kind;
            }
            else {

                $res_reg := $*REGALLOC.fresh_register($kind);
                $res_kind := $kind;
                push_op(@ins, 'get' ~ @attr_opnames[$kind], $res_reg, $obj.result_reg,
                    $han.result_reg, MAST::SVal.new( :value($name) ),
                        MAST::IVal.new( :value(-1) ) );
            }
            $*REGALLOC.release_register($obj.result_reg, $MVM_reg_obj);
            $*REGALLOC.release_register($han.result_reg, $MVM_reg_obj);
        }
        elsif $scope eq 'positional' {
            return self.as_mast_clear_bindval($*BINDVAL
                ?? QAST::Op.new( :op('positional_bind'), |$node.list, $*BINDVAL)
                !! QAST::Op.new( :op('positional_get'), |$node.list));
        }
        elsif $scope eq 'associative' {
            return self.as_mast_clear_bindval($*BINDVAL
                ?? QAST::Op.new( :op('associative_bind'), |$node.list, $*BINDVAL)
                !! QAST::Op.new( :op('associative_get'), |$node.list));
        }
        else {
            nqp::die("QAST::Var with scope '$scope' NYI");
        }

        MAST::InstructionList.new(@ins, $res_reg, $res_kind)
    }

    multi method as_mast(MAST::InstructionList $ilist, :$want) {
        $ilist
    }

    multi method as_mast(MAST::Node $node, :$want) {
        MAST::InstructionList.new([$node], MAST::VOID, $MVM_reg_void)
    }

    method as_mast_clear_bindval($node, :$want) {
        my $*BINDVAL := 0;
        nqp::defined($want) ?? self.as_mast($node, :$want) !! self.as_mast($node)
    }

    proto method as_mast_constant($qast) { * }

    multi method as_mast_constant(QAST::IVal $iv) {
        MAST::IVal.new( :value($iv.value) )
    }
    multi method as_mast_constant(QAST::SVal $sv) {
        MAST::SVal.new( :value($sv.value) )
    }
    multi method as_mast_constant(QAST::NVal $nv) {
        MAST::NVal.new( :value($nv.value) )
    }
    multi method as_mast_constant(QAST::Node $qast) {
        nqp::die("expected QAST constant; didn't get one");
    }

    multi method as_mast(QAST::IVal $iv, :$want) {
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

    multi method as_mast(QAST::NVal $nv, :$want) {
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

    sub const_s($val) {
        my $reg := $*REGALLOC.fresh_s();
        MAST::InstructionList.new(
            [MAST::Op.new(
                :bank('primitives'), :op('const_s'),
                $reg,
                MAST::SVal.new( :value($val) )
            )],
            $reg,
            $MVM_reg_str)
    }

    multi method as_mast(QAST::SVal $sv, :$want) {
        const_s($sv.value)
    }

    multi method as_mast(QAST::BVal $bv, :$want) {
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

    multi method as_mast(QAST::WVal $node, :$want) {
        my $val    := $node.value;
        my $sc     := nqp::getobjsc($val);
        my $idx    := nqp::scgetobjidx($sc, $val);
        my $sc_idx := $*MAST_COMPUNIT.sc_idx($sc);
        my $reg    := $*REGALLOC.fresh_o();
        my $op     := $idx < 32768 ?? 'wval' !! 'wval_wide';
        MAST::InstructionList.new(
            [MAST::Op.new(
                :bank('serialization'), :op($op),
                $reg,
                MAST::IVal.new( :value($sc_idx) ),
                MAST::IVal.new( :value($idx) )
            )],
            $reg,
            $MVM_reg_obj)
    }

    multi method as_mast(QAST::Regex $node, :$want) {
        nqp::defined($want)
            ?? QAST::MASTRegexCompiler.new().as_mast($node, :want($want))
            !! QAST::MASTRegexCompiler.new().as_mast($node)
    }

    multi method as_mast($unknown, :$want) {
        nqp::die("Unknown QAST node type " ~ $unknown.HOW.name($unknown));

        #my $name := 'null';
        #try $name := nqp::isnull($unknown) || nqp::isnull($unknown.HOW) ?? nqp::die('fail') !! $unknown.HOW.name($unknown);
        #$name := pir::typeof__SP($unknown) unless nqp::isnull($unknown) || $name ne 'null';
        #nqp::die("Unknown QAST node type $name");
    }

    method annotated($ilist, $file, $line) {
        MAST::InstructionList.new([
            MAST::Annotated.new(:file($file), :line($line),
                :instructions($ilist.instructions))],
            $ilist.result_reg, $ilist.result_kind)
    }

    my @prim_to_reg := [$MVM_reg_obj, $MVM_reg_int64, $MVM_reg_num64, $MVM_reg_str];
    method type_to_register_kind($type) {
        @prim_to_reg[nqp::objprimspec($type)]
    }

    method operations() {
        QAST::MASTOperations
    }
}

sub push_op(@dest, $op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        next if ~$_ eq '$allops';
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    $op := $op.name if nqp::istype($op, QAST::Op);
    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);

    nqp::push(@dest, MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    ));
}
