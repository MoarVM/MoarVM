use QASTOperationsMAST;
use NQPCursorQAST;

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
    
    method new() {
        my $obj := nqp::create(self);
        $obj.BUILD();
        $obj
    }
    
    method BUILD() {
    }
    
    method as_mast($qast) {
        # Prefix for the regexes code pieces.
        my $prefix := $*QASTCOMPILER.unique('rx') ~ '_';

        # Build the list of (unique) registers we need
        my %*REG := nqp::hash(
            'tgt', fresh_s(),
            'pos', fresh_i(),
            'off', fresh_i(),
            'eos', fresh_i(),
            'rep', fresh_i(),
            'cur', fresh_o(),
            'curclass', fresh_o(),
            'bstack', fresh_o(),
            'cstack', fresh_o());

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
    
    method regex_mast($node) {
        return $*QASTCOMPILER.as_mast($node) unless $node ~~ QAST::Regex;
        my $rxtype := $node.rxtype() || 'concat';
        self."$rxtype"($node)
    }
}

#sub op(@dest, $op, *@args) {
#    # Resolve the op.
#    my $bank;
#    for MAST::Ops.WHO {
#        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
#    }
#    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);
#    
#    nqp::push(@dest, MAST::Op.new(
#        :bank(nqp::substr($bank, 1)), :op($op),
#        |@args
#    ));
#}
#
#sub call(@ins, $target, @flags, :$result, *@args) {
#    nqp::push(@ins, MAST::Call.new(
#            :target($target), :result($result), :flags(@flags), |@args
#        ));
#}
#
#sub release($reg, $type) { $*REGALLOC.release_register($reg, $type) }
#
#sub fresh_i() { $*REGALLOC.fresh_i() }
#sub fresh_n() { $*REGALLOC.fresh_n() }
#sub fresh_s() { $*REGALLOC.fresh_s() }
#sub fresh_o() { $*REGALLOC.fresh_o() }
#
#sub label($name) { MAST::Label.new( :name($name) ) }
