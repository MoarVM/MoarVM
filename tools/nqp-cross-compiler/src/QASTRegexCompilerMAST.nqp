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

class QAST::MASTRegexCompiler {
    
    has $!qastcomp;
    
    method new($compiler) {
        my $obj := nqp::create(self);
        $obj.BUILD($compiler);
        $obj
    }
    
    method BUILD($compiler) {
        $!qastcomp := $compiler;
    }
    
    method as_mast($qast) {
        # Prefix for the regexes code pieces.
        my $prefix := $!qastcomp.unique('rx') ~ '_';

        # Build the list of (unique) registers we need
        my %*REG := nqp::hash(
            'tgt', fresh_s,
            'pos', fresh_i,
            'off', fresh_i,
            'eos', fresh_i,
            'rep', fresh_i,
            'cur', fresh_o,
            'curclass', fresh_o,
            'bstack', fresh_o,
            'cstack', fresh_o);

        # create our labels
        my $startlabel   := label($prefix ~ 'start');
        my $donelabel    := label($prefix ~ 'done');
        my $restartlabel := label($prefix ~ 'restart');
        my $faillabel    := label($prefix ~ 'fail');
        my $jumplabel    := label($prefix ~ 'jump');
        my $cutlabel     := label($prefix ~ 'cut');
        my $cstacklabel  := label($prefix ~ 'cstack_done');
        %*REG<fail>      := $faillabel;
        
        my @ins := nqp::list();
        

        nqp::die("Regex compilation NYI");
    }
    
    method build_regex_types() {
        my @ins := nqp::list();
        
        my $cursor_type := simple_type_from_repr(@ins, 'NQPCursor', 'P6opaque');
        
        add_method(@ins, $!qastcomp, $cursor_type, 'foo', QAST::Block.new(
            
        ));
        
        compose(@ins, $cursor_type);
        
        MAST::InstructionList.new(@ins, MAST::VOID, $MVM_reg_void)
    }
}

sub ival($val) { MAST::IVal.new( :value($val) ) }
sub nval($val) { MAST::NVal.new( :value($val) ) }
sub sval($val) { MAST::SVal.new( :value($val) ) }

sub op(@dest, $op, *@args) {
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

sub call(@ins, $target, @flags, :$result, *@args) {
    nqp::push(@ins, MAST::Call.new(
            :target($target), :result($result), :flags(@flags), |@args
        ));
}

sub release($reg, $type) { $*REGALLOC.release_register($reg, $type) }

sub fresh_i() { $*REGALLOC.fresh_i() }
sub fresh_n() { $*REGALLOC.fresh_n() }
sub fresh_s() { $*REGALLOC.fresh_s() }
sub fresh_o() { $*REGALLOC.fresh_o() }

sub label($name) { MAST::Label.new( :name($name) ) }

sub simple_type_from_repr(@ins, $name_str, $repr_str) {
    my $name := fresh_s();
    my $repr := fresh_s();
    my $how  := fresh_o();
    my $type := fresh_o();
    my $meth := fresh_o();
    
    # Create the type.
    op(@ins, 'const_s', $name, sval($name_str));
    op(@ins, 'const_s', $repr, sval($repr_str));
    op(@ins, 'knowhow', $how);
    op(@ins, 'findmeth', $meth, $how, sval('new_type'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::str],
        $how, sval('name'), $name, sval('repr'), $repr, :result($type));
    
    release($name, $MVM_reg_str);
    release($repr, $MVM_reg_str);
    release($how, $MVM_reg_obj);
    release($meth, $MVM_reg_obj);
    
    $type
}

# Called with Int/Num/Str. Makes a P6opaque-based type that boxes the
# appropriate native.
sub boxing_type(@ins, $name_str) {
    # Create the unboxed type.
    my $ntype := simple_type_from_repr(@ins, nqp::lc($name_str), 'P6' ~ nqp::lc($name_str));
    
    my $name := fresh_s();
    my $bt   := fresh_i();
    my $how  := fresh_o();
    my $type := fresh_o();
    my $meth := fresh_o();
    my $attr := fresh_o();
    
    # Create the type.
    op(@ins, 'const_s', $name, sval($name_str));
    op(@ins, 'knowhow', $how);
    op(@ins, 'findmeth', $meth, $how, sval('new_type'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str], $how, sval('name'), $name, :result($type));
    
    # Add an attribute.
    op(@ins, 'gethow', $how, $type);
    op(@ins, 'knowhowattr', $attr);
    op(@ins, 'const_s', $name, sval('$!foo'));
    op(@ins, 'const_i64', $bt, ival(1));
    op(@ins, 'findmeth', $meth, $attr, sval('new'));
    call(@ins, $meth, [$Arg::obj, $Arg::named +| $Arg::str, $Arg::named +| $Arg::obj, $Arg::named +| $Arg::int],
        $attr, sval('name'), $name, sval('type'), $ntype, sval('box_target'), $bt, :result($attr));
    op(@ins, 'findmeth', $meth, $how, sval('add_attribute'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj, $Arg::obj], $how, $type, $attr, :result($attr));
    
    release($name, $MVM_reg_str);
    release($bt, $MVM_reg_int64);
    release($how, $MVM_reg_obj);
    release($ntype, $MVM_reg_obj);
    release($meth, $MVM_reg_obj);
    release($attr, $MVM_reg_obj);
    
    $type
}

sub add_method(@ins, $compiler, $type, $name_str, $qast) {
    my $unused := $compiler.as_mast($qast);
    my $mast := $compiler.as_mast(QAST::BVal.new(:value($qast)));
    my $code := $mast.result_reg;
    nqp::splice(@ins, $mast.instructions, +@ins, 0);
    my $name := fresh_s();
    
    my $how := fresh_o();
    my $meth := fresh_o();
    
    op(@ins, 'const_s', $name, sval($name_str));
    op(@ins, 'gethow', $how, $type);
    op(@ins, 'findmeth', $meth, $how, sval('add_method'));
    call(@ins, $meth,
        [$Arg::obj, $Arg::obj, $Arg::str, $Arg::obj],
        $how, $type, $name, $code, :result($meth));
    
    release($how, $MVM_reg_obj);
    release($meth, $MVM_reg_obj);
    release($code, $MVM_reg_obj);
}

sub compose(@ins, $type) {
    # Compose.
    my $how := fresh_o();
    my $meth := fresh_o();
    op(@ins, 'gethow', $how, $type);
    op(@ins, 'findmeth', $meth, $how, sval('compose'));
    call(@ins, $meth, [$Arg::obj, $Arg::obj], $how, $type, :result($type));
    
    release($how, $MVM_reg_obj);
    release($meth, $MVM_reg_obj);
}
