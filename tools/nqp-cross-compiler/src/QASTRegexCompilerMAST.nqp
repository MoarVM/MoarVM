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
    
    method as_mast($node) {
        # Prefix for the regexes code pieces.
        my $prefix := $*QASTCOMPILER.unique('rx') ~ '_';
        my $*RXPREFIX := $prefix;

        # Build the list of (unique) registers we need
        my $tgt      := fresh_s();
        my $pos      := fresh_i();
        my $off      := fresh_i();
        my $eos      := fresh_i();
        my $rep      := fresh_i();
        my $cur      := fresh_o();
        my $curclass := fresh_o();
        my $bstack   := fresh_o();
        my $cstack   := fresh_o();
        my $negone   := fresh_i();
        my $zero     := fresh_i();
        my $one      := fresh_i();
        my $two      := fresh_i();
        my $three    := fresh_i();
        my $four     := fresh_i();
        my $P11      := fresh_o();
        my $method   := fresh_o();
        
        # create our labels
        my $startlabel   := label($prefix ~ 'start');
        my $donelabel    := label($prefix ~ 'done');
        my $restartlabel := label($prefix ~ 'restart');
        my $faillabel    := label($prefix ~ 'fail');
        my $jumplabel    := label($prefix ~ 'jump');
        my $cutlabel     := label($prefix ~ 'cut');
        my $cstacklabel  := label($prefix ~ 'cstack_done');
        
        my %*REG := nqp::hash(
            'tgt',      $tgt,
            'pos',      $pos,
            'off',      $off,
            'eos',      $eos,
            'rep',      $rep,
            'cur',      $cur,
            'curclass', $curclass,
            'bstack',   $bstack,
            'cstack',   $cstack,
            'negone',   $negone,
            'zero',     $zero,
            'one',      $one,
            'two',      $two,
            'three',    $three,
            'four',     $four,
            'P11',      $P11,
            'fail',     $faillabel,
            'jump',     $jumplabel,
            'method',   $method);
        
        my @*RXJUMPS := nqp::list();
        
        my $self := $*BLOCK.local('self'); # MAST::Local
        my $cstarttype_lex := $*BLOCK.resolve_lexical('CursorStart'); # MAST::Lexical
        my $cstarttype := fresh_o();
        my $cstart := fresh_o();
        my $i19 := fresh_i(); # yes, I know, inheriting the name from ancestor method
        # XXX TODO actually use the correct cursor symbol somehow
        my $cursor_lex := MAST::Lexical.new( :index($*MAST_FRAME.add_lexical(NQPMu, '=Cursor')) );
        ($*BLOCK.lexicals()){'=Cursor'} := $cursor_lex;
        ($*BLOCK.lexical_kinds()){'=Cursor'} := $MVM_reg_obj;
        my $i0 := fresh_i();
        
        my @ins := [
            op('const_i', $negone, ival(-1)),
            op('const_i', $zero, ival(0)),
            op('const_i', $one, ival(1)),
            op('const_i', $two, ival(2)),
            op('const_i', $three, ival(3)),
            op('const_i', $four, ival(4)),
            op('getlex', $cstarttype, $cstarttype_lex),
            op('findmeth', $method, $self, sval('!cursor_start')),
            call($method, [ $Arg::obj ], :result($cstart), $self ),
            op('getattr_o', $cur, $cstart, $cstarttype, sval('$!cur'), ival(-1)),
            op('getattr_s', $tgt, $cstart, $cstarttype, sval('$!tgt'), ival(-1)),
            op('getattr_i', $pos, $cstart, $cstarttype, sval('$!pos'), ival(-1)),
            op('getattr_o', $curclass, $cstart, $cstarttype, sval('$!curclass'), ival(-1)),
            op('getattr_o', $bstack, $cstart, $cstarttype, sval('$!bstack'), ival(-1)),
            op('getattr_i', $i19, $cstart, $cstarttype, sval('$!i19'), ival(-1)),
            op('bindlex', $cursor_lex, $cur),
            op('graphs_s', $eos, $tgt),
            op('eq_i', $i0, $one, $i19),
            op('if_i', $i0, $restartlabel),
            op('gt_i', $i0, $pos, $eos),
            op('if_i', $i0, $faillabel)
        ];
        release($i0, $MVM_reg_int64);
        release($i19, $MVM_reg_int64);
        
        merge_ins(@ins, self.regex_mast($node));
        
        $i0 := fresh_i();
        $i19 := fresh_i();
        my $i18 := fresh_i();
        merge_ins(@ins, [
            $restartlabel,
            op('getattr_o', $cstack, $cur, $curclass, sval('$!cstack'), ival(-1)),
            $faillabel,
            op('isnull', $i0, $bstack),
            op('if_i', $i0, $donelabel),
            op('elemspos', $i0, $bstack),
            op('gt_i', $i0, $i0, $zero),
            op('unless_i', $i0, $donelabel),
            op('pop_i', $i19, $bstack),
            op('isnull', $i0, $cstack),
            op('if_i', $i0, $cstacklabel),
            op('elemspos', $i0, $cstack),
            op('gt_i', $i0, $i0, $zero),
            op('unless_i', $i0, $cstacklabel),
            op('dec_i', $i19),
            op('atpos_o', $P11, $cstack, $i19),
            $cstacklabel,
            op('pop_i', $rep, $bstack),
            op('pop_i', $pos, $bstack),
            op('pop_i', $i19, $bstack),
            op('lt_i', $i0, $pos, $negone),
            op('if_i', $i0, $donelabel),
            op('lt_i', $i0, $pos, $zero),
            op('if_i', $i0, $faillabel),
            op('eq_i', $i0, $i19, $zero),
            op('if_i', $i0, $faillabel),
            # backtrack the cursor stack
            op('isnull', $i0, $cstack),
            op('if_i', $i0, $jumplabel),
            op('elemspos', $i18, $bstack),
            op('le_i', $i0, $i18, $zero),
            op('if_i', $i0, $cutlabel),
            op('dec_i', $i18),
            op('atpos_i', $i18, $bstack, $i18),
            $cutlabel,
            op('setelemspos', $cstack, $i18),
            $jumplabel,
            op('jumplist', ival(+@*RXJUMPS), $i19)
        ]);
        nqp::push(@ins, op('goto', $_)) for @*RXJUMPS;
        merge_ins(@ins, [
            $donelabel,
            op('findmeth', $method, $cur, sval('!cursor_fail')),
            call($method, [ $Arg::obj ], $cur), # don't pass a :result so it's void
        ]);
        
        MAST::InstructionList.new(@ins, $cur, $MVM_reg_obj)
    }
    
    method alt($node) {
        unless $node.name {
            return self.altseq($node);
        }
        
        # Calculate all the branches to try, which populates the bstack
        # with the options. Then immediately fail to start iterating it.
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_alt') ~ '_';
        my $endlabel_index := rxjump($prefix ~ 'end');
        my $endlabel := @*RXJUMPS[$endlabel_index];
        my @ins := nqp::list();
        my @label_ins := nqp::list();
        #nqp::push(@label_ins, op('create', %*REG<P11>, %*REG<IARRTYPE>); # XXX new integer array
        self.regex_mark(@ins, $endlabel, %*REG<negone>, %*REG<zero>);
        nqp::push(@ins, op('findmeth', %*REG<method>, %*REG<cur>, '!alt'));
        my $name := fresh_s();
        nqp::push(@ins, op('const_s', $name, sval($node.name)));
        nqp::push(@ins, call(%*REG<method>, [ $Arg::obj, $Arg::int, $Arg::str, $Arg::obj ],
            %*REG<cur>, %*REG<pos>, $name, %*REG<P11>));
        release($name, $MVM_reg_str);
        nqp::push(@ins, op('goto', %*REG<fail>));
        
        # Emit all the possible alternatives
        my $altcount := 0;
        my $iter     := nqp::iterator($node.list);
        while $iter {
            my $altlabel_index := rxjump($prefix ~ $altcount);
            my $altlabel := @*RXJUMPS[$altlabel_index];
            my @amast    := self.regex_mast(nqp::shift($iter));
            nqp::push(@ins, $altlabel);
            merge_ins(@ins, @amast);
            nqp::push(@ins, op('goto', $endlabel));
            nqp::push(@label_ins, op('push_i', %*REG<P11>, $altlabel_index));
            $altcount++;
        }
        nqp::push(@ins, $endlabel);
        self.regex_commit(@ins, $endlabel_index) if $node.backtrack eq 'r';
        merge_ins(@label_ins, @ins);
        @label_ins # so the label array creation happens first
    }
    
    method altseq($node) {
        my @ins := nqp::list();
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_altseq') ~ '_';
        my $altcount := 0;
        my $iter := nqp::iterator($node.list);
        my $endlabel_index := rxjump($prefix ~ 'end');
        my $endlabel := @*RXJUMPS[$endlabel_index];
        my $altlabel_index := rxjump($prefix ~ $altcount);
        my $altlabel := @*RXJUMPS[$altlabel_index];
        my @amast    := self.regex_mast(nqp::shift($iter));
        while $iter {
            nqp::push(@ins, $altlabel);
            $altcount++;
            $altlabel_index := rxjump($prefix ~ $altcount);
            $altlabel := @*RXJUMPS[$altlabel_index];
            self.regex_mark(@ins, $altlabel_index, %*REG<pos>, %*REG<zero>);
            merge_ins(@ins, @amast);
            nqp::push(@ins, op('goto', $endlabel));
            @amast := self.regex_mast(nqp::shift($iter));
        }
        nqp::push(@ins, $altlabel);
        merge_ins(@ins, @amast);
        nqp::push(@ins, $endlabel);
        @ins
    }
    
    method concat($node) {
        my @ins := nqp::list();
        merge_ins(@ins, self.regex_mast($_)) for $node.list;
        @ins
    }
    
    method conj($node) { self.conjseq($node) }
    
    method conjseq($node) {
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_rxconj') ~ '_';
        my $conjlabel_index := rxjump($prefix ~ 'fail');
        my $conjlabel := @*RXJUMPS[$conjlabel_index];
        my $firstlabel := label($prefix ~ 'first');
        my $iter := nqp::iterator($node.list);
        # make a mark that holds our starting position in the pos slot
        self.regex_mark(@ins, $conjlabel, %*REG<pos>, %*REG<zero>);
        my @ins := [
            op('goto', $firstlabel),
            $conjlabel,
            op('goto', %*REG<fail>),
            # call the first child
            $firstlabel
        ];
        merge_ins(@ins, self.regex_mast(nqp::shift($iter)));
        # use previous mark to make one with pos=start, rep=end
        my $i11 := fresh_i();
        my $i12 := fresh_i();
        self.regex_peek(@ins, $conjlabel, $i11);
        self.regex_mark(@ins, $conjlabel, $i11, %*REG<pos>);
        
        while $iter {
            nqp::push(@ins, op('set', %*REG<pos>, $i11));
            merge_ins(@ins, self.regex_mast(nqp::shift($iter)));
            self.regex_peek(@ins, $conjlabel, $i11, $i12);
            nqp::push(@ins, op('ne_i', $i12, %*REG<pos>, $i12));
            nqp::push(@ins, op('if_i', $i12, %*REG<fail>));
        }
        nqp::push(@ins, op('set', %*REG<pos>, $i11)) if $node.subtype eq 'zerowidth';
        release($i11, $MVM_reg_int64);
        release($i12, $MVM_reg_int64);
        @ins
    }
    
    method regex_mark(@ins, $label_index, $pos, $rep) {
        my $bstack := %*REG<bstack>;
        my $mark := fresh_i();
        my $elems := fresh_i();
        my $caps := fresh_i();
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_rxmark');
        my $haselemslabel := label($prefix ~ '_haselems');
        my $haselemsendlabel := label($prefix ~ '_haselemsend');
        merge_ins(@ins, [
            op('const_i', $mark, ival($label_index)),
            op('elemspos', $elems, $bstack),
            op('gt_i', $caps, $elems, %*REG<zero>),
            op('if_i', $caps, $haselemslabel),
            op('set', $caps, %*REG<zero>),
            op('goto', $haselemsendlabel),
            $haselemslabel,
            op('dec_i', $elems),
            op('atpos_i', $caps, $bstack, $elems),
            $haselemsendlabel,
            op('push_i', $bstack, $mark),
            op('push_i', $bstack, $pos),
            op('push_i', $bstack, $rep),
            op('push_i', $bstack, $caps)
        ]);
        release($mark, $MVM_reg_int64);
        release($elems, $MVM_reg_int64);
        release($caps, $MVM_reg_int64);
    }
    
    method regex_peek(@ins, $label_index, *@regs) {
        my $bstack := %*REG<bstack>;
        my $mark := fresh_i();
        my $ptr := fresh_i();
        my $i0 := fresh_i();
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_rxpeek');
        my $haselemsendlabel := label($prefix ~ '_haselemsend');
        my $backupendlabel := label($prefix ~ '_backupend');
        merge_ins(@ins, [
            op('const_i', $mark, ival($label_index)),
            op('elemspos', $ptr, $bstack),
            $haselemsendlabel,
            op('lt_i', $i0, $ptr, %*REG<zero>),
            op('if_i', $i0, $backupendlabel),
            op('atpos_i', $i0, $bstack, $ptr),
            op('eq_i', $i0, $i0, $mark),
            op('if_i', $i0, $backupendlabel),
            op('sub_i', $ptr, $ptr, %*REG<four>),
            op('goto', $haselemsendlabel),
            $backupendlabel
        ]);
        for @regs {
            nqp::push(@ins, op('inc_i', $ptr));
            nqp::push(@ins, op('atpos_i', $_, $bstack, $ptr)) if $_ ne '*';
        }
        release($mark, $MVM_reg_int64);
        release($ptr, $MVM_reg_int64);
        release($i0, $MVM_reg_int64);
    }
    
    method regex_commit(@ins, $label_index) {
        my $bstack := %*REG<bstack>;
        my $mark := fresh_i();
        my $ptr := fresh_i();
        my $caps := fresh_i();
        my $i0 := fresh_i();
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_rxcommit');
        my $haselemslabel := label($prefix ~ '_haselems');
        my $haselemsendlabel := label($prefix ~ '_haselemsend');
        my $backupendlabel := label($prefix ~ '_backupend');
        my $nocapslabel := label($prefix ~ '_nocaps');
        my $makemarklabel := label($prefix ~ '_makemark');
        merge_ins(@ins, [
            op('const_i', $mark, ival($label_index)),
            op('elemspos', $ptr, $bstack),
            op('gt_i', $caps, $ptr, %*REG<zero>),
            op('if_i', $caps, $haselemslabel),
            op('set', $caps, %*REG<zero>),
            op('goto', $haselemsendlabel),
            $haselemslabel,
            op('dec_i', $ptr),
            op('atpos_i', $caps, $bstack, $ptr),
            op('inc_i', $ptr),
            $haselemsendlabel,
            op('lt_i', $i0, $ptr, %*REG<zero>),
            op('if_i', $i0, $backupendlabel),
            op('atpos_i', $i0, $bstack, $ptr),
            op('eq_i', $i0, $i0, $mark),
            op('if_i', $i0, $backupendlabel),
            op('sub_i', $ptr, $ptr, %*REG<four>),
            op('goto', $haselemsendlabel),
            $backupendlabel,
            op('setelemspos', $bstack, $ptr),
            op('lt_i', $i0, $caps, %*REG<one>),
            op('if_i', $i0, $nocapslabel),
            op('lt_i', $i0, $ptr, %*REG<one>),
            op('if_i', $makemarklabel),
            op('sub_i', $ptr, $ptr, %*REG<three>),
            op('atpos_i', $i0, $bstack, $ptr),
            op('ge_i', $i0, $i0, %*REG<zero>),
            op('if_i', $i0, $makemarklabel),
            op('add_i', $ptr, $ptr, %*REG<two>),
            op('bindpos_i', $bstack, $ptr, $caps),
            op('inc_i', $ptr),
            op('goto', $nocapslabel),
            $makemarklabel,
            op('push_i', $bstack, %*REG<zero>),
            op('push_i', $bstack, %*REG<negone>),
            op('push_i', $bstack, %*REG<zero>),
            op('push_i', $bstack, $caps),
            $nocapslabel
        ]);
        release($mark, $MVM_reg_int64);
        release($ptr, $MVM_reg_int64);
        release($caps, $MVM_reg_int64);
        release($i0, $MVM_reg_int64);
    }
    
    method regex_mast($node) {
        return $*QASTCOMPILER.as_mast($node) unless $node ~~ QAST::Regex;
        my $rxtype := $node.rxtype() || 'concat';
        self."$rxtype"($node) # expects to return an nqp::list of instructions
    }
}

sub rxjump($name) {
    my $index := +@*RXJUMPS;
    @*RXJUMPS[$index] :=  MAST::Label.new( :name($name) );
    $index
}

sub merge_ins(@dest, @src) {
    nqp::splice(@dest, @src, +@dest, 0);
}

sub op($op, *@args) {
    # Resolve the op.
    my $bank;
    for MAST::Ops.WHO {
        $bank := ~$_ if nqp::existskey(MAST::Ops.WHO{~$_}, $op);
    }
    nqp::die("Unable to resolve MAST op '$op'") unless nqp::defined($bank);
    
    MAST::Op.new(
        :bank(nqp::substr($bank, 1)), :op($op),
        |@args
    );
}

sub call($target, @flags, :$result, *@args) {
    MAST::Call.new(
        :target($target), :result($result), :flags(@flags), |@args
    );
}

sub release($reg, $type) { $*REGALLOC.release_register($reg, $type) }

sub fresh_i() { $*REGALLOC.fresh_i() }
sub fresh_n() { $*REGALLOC.fresh_n() }
sub fresh_s() { $*REGALLOC.fresh_s() }
sub fresh_o() { $*REGALLOC.fresh_o() }

sub label($name) { MAST::Label.new( :name($name) ) }
sub ival($val) { MAST::IVal.new( :value($val) ) }
sub nval($val) { MAST::NVal.new( :value($val) ) }
sub sval($val) { MAST::SVal.new( :value($val) ) }
