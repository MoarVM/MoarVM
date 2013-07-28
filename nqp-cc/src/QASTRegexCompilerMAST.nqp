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

    method new() {
        my $obj := nqp::create(self);
        $obj.BUILD();
        $obj
    }

    method BUILD() {
    }

    method unique($str?) {
        $*QASTCOMPILER.unique($str)
    }

    method as_mast($node, :$want) {
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
        my $five     := fresh_i();
        my $P11      := fresh_o();
        my $method   := fresh_o();
        my $tmp      := fresh_o();

        # cclass_const
        my $cclass_word     := fresh_i();
        my $cclass_newline  := fresh_i();

        # create our labels
        my $startlabel   := label($prefix ~ 'start');
        my $donelabel    := label($prefix ~ 'done');
        my $restartlabel := label($prefix ~ 'restart');
        my $faillabel    := label($prefix ~ 'fail');
        my $jumplabel    := label($prefix ~ 'jump');
        my $cutlabel     := label($prefix ~ 'cut');
        my $cstacklabel  := label($prefix ~ 'cstack_done');

        my $self := $*BLOCK.local('self');

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
            'method',   $method,
            'self',     $self,
            'cclass_word'   , $cclass_word,
            'cclass_newline', $cclass_newline
            );

        my @*RXJUMPS := nqp::list($donelabel);

        my $cstart := fresh_o();
        my $i19 := fresh_i(); # yes, I know, inheriting the name from ancestor method
        my $i0 := fresh_i();

        my @ins := [
            op('const_i64', $negone, ival(-1)),
            op('const_i64', $zero, ival(0)),
            op('const_i64', $one, ival(1)),
            op('const_i64', $two, ival(2)),
            op('const_i64', $three, ival(3)),
            op('const_i64', $four, ival(4)),
            op('const_i64', $five, ival(5)),
            op('const_i64', $cclass_word, ival(nqp::const::CCLASS_WORD)),
            op('const_i64', $cclass_newline, ival(nqp::const::CCLASS_NEWLINE)),
            op('findmeth', $method, $self, sval('!cursor_start_all')),
            call($method, [ $Arg::obj ], :result($cstart), $self ),
            op('atpos_o', $cur, $cstart, $zero),
            op('atpos_o', $tmp, $cstart, $one),
            op('unbox_s', $tgt, $tmp),
            op('flattenropes', $tgt),
            op('atpos_o', $tmp, $cstart, $two),
            op('unbox_i', $pos, $tmp),
            op('atpos_o', $curclass, $cstart, $three),
            op('atpos_o', $bstack, $cstart, $four),
            op('atpos_o', $tmp, $cstart, $five),
            op('unbox_i', $i19, $tmp),
            op('bindlex', $*BLOCK.resolve_lexical('$¢'), $cur),
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
            op('elems', $i0, $bstack),
            op('gt_i', $i0, $i0, $zero),
            op('unless_i', $i0, $donelabel),
            op('pop_i', $i19, $bstack),
            op('isnull', $i0, $cstack),
            op('if_i', $i0, $cstacklabel),
            op('elems', $i0, $cstack),
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
            op('elems', $i18, $bstack),
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

    method children($node) {
        my @masts := nqp::list();
        my @results := nqp::list();
        my @result_kinds := nqp::list();
        for @($node) {
            my $mast := $*QASTCOMPILER.as_mast($_);
            merge_ins(@masts, $mast.instructions);
            nqp::push(@results, $mast.result_reg);
            nqp::push(@result_kinds, $mast.result_kind);
        }
        [@masts, @results, @result_kinds]
    }

    method alt($node) {
        unless $node.name {
            return self.altseq($node);
        }

        # Calculate all the branches to try, which populates the bstack
        # with the options. Then immediately fail to start iterating it.
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_alt');
        my $endlabel_index := rxjump($prefix ~ '_end');
        my $endlabel := @*RXJUMPS[$endlabel_index];
        my @ins := nqp::list();
        my @label_ins := nqp::list();
        nqp::push(@label_ins, op('bootintarray', %*REG<P11>));
        nqp::push(@label_ins, op('create', %*REG<P11>, %*REG<P11>));
        self.regex_mark(@ins, $endlabel_index, %*REG<negone>, %*REG<zero>);
        nqp::push(@ins, op('findmeth', %*REG<method>, %*REG<cur>, sval('!alt')));
        my $name := fresh_s();
        nqp::push(@ins, op('const_s', $name, sval($node.name)));
        nqp::push(@ins, call(%*REG<method>, [ $Arg::obj, $Arg::int, $Arg::str, $Arg::obj ],
            %*REG<cur>, %*REG<pos>, $name, %*REG<P11>));
        release($name, $MVM_reg_str);
        nqp::push(@ins, op('goto', %*REG<fail>));

        # Emit all the possible alternatives
        my $altcount := 0;
        my $iter     := nqp::iterator($node.list);
        my $itmp     := fresh_i();
        while $iter {
            my $altlabel_index := rxjump($prefix ~ $altcount);
            my $altlabel := @*RXJUMPS[$altlabel_index];
            my @amast    := self.regex_mast(nqp::shift($iter));
            nqp::push(@ins, $altlabel);
            merge_ins(@ins, @amast);
            nqp::push(@ins, op('goto', $endlabel));
            nqp::push(@label_ins, op('const_i64', $itmp, ival($altlabel_index)));
            nqp::push(@label_ins, op('push_i', %*REG<P11>, $itmp));
            $altcount++;
        }
        release($itmp, $MVM_reg_int64);
        nqp::push(@ins, $endlabel);
        self.regex_commit(@ins, $endlabel_index) if $node.backtrack eq 'r';
        merge_ins(@label_ins, @ins);
        @label_ins # so the label array creation happens first
    }

    method altseq($node) {
        my @ins := nqp::list();
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_altseq');
        my $altcount := 0;
        my $iter := nqp::iterator($node.list);
        my $endlabel_index := rxjump($prefix ~ '_end');
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

    method anchor($node) {
        my @ins := nqp::list();
        my $subtype := $node.subtype;
        my $donelabel := label(self.unique($*RXPREFIX ~ '_rxanchor') ~ '_done');
        my $i11 := fresh_i();
        my $pos := %*REG<pos>;
        my $fail := %*REG<fail>;
        if $subtype eq 'bos' {
            nqp::push(@ins, op('ne_i', $i11, $pos, %*REG<zero>));
            nqp::push(@ins, op('if_i', $i11, $fail));
        }
        elsif $subtype eq 'eos' {
            nqp::push(@ins, op('lt_i', $i11, $pos, %*REG<eos>));
            nqp::push(@ins, op('if_i', $i11, $fail));
        }
        elsif $subtype eq 'lwb' {
            merge_ins(@ins, [
                op('ge_i', $i11, $pos, %*REG<eos>),
                op('if_i', $i11, $fail),
                op('is_cclass', $i11, %*REG<cclass_word>, %*REG<tgt>, $pos),
                op('unless_i', $i11, %*REG<fail>),
                op('sub_i', $i11, %*REG<pos>, %*REG<one>),
                op('is_cclass', $i11, %*REG<cclass_word>, %*REG<tgt>, $i11),
                op('if_i', $i11, $fail)
            ]);
        }
        elsif $subtype eq 'rwb' {
            merge_ins(@ins, [
                op('le_i', $i11, $pos, %*REG<zero>),
                op('if_i', $i11, $fail),
                op('is_cclass', $i11, %*REG<cclass_word>, %*REG<tgt>, $pos),
                op('if_i', $i11, %*REG<fail>),
                op('sub_i', $i11, %*REG<pos>, %*REG<one>),
                op('is_cclass', $i11, %*REG<cclass_word>, %*REG<tgt>, $i11),
                op('unless_i', $i11, $fail)
            ]);
        }
        elsif $subtype eq 'bol' {
            merge_ins(@ins, [
                op('eq_i', $i11, %*REG<pos>, %*REG<zero>),
                op('if_i', $i11, $donelabel),
                op('ge_i', $i11, $pos, %*REG<eos>),
                op('if_i', $i11, $fail),
                op('sub_i', $i11, %*REG<pos>, %*REG<one>),
                op('is_cclass', $i11, %*REG<cclass_newline>, %*REG<tgt>, $i11),
                op('unless_i', $i11, $fail),
                $donelabel
            ]);
        }
        elsif $subtype eq 'eol' {
            merge_ins(@ins, [
                op('is_cclass', $i11, %*REG<cclass_newline>, %*REG<tgt>, %*REG<pos>),
                op('if_i', $i11, $donelabel),
                op('ne_i', $i11, %*REG<pos>, %*REG<eos>),
                op('if_i', $i11, $fail),
                op('eq_i', $i11, %*REG<pos>, %*REG<zero>),
                op('if_i', $i11, $donelabel),
                op('sub_i', $i11, %*REG<pos>, %*REG<one>),
                op('is_cclass', $i11, %*REG<cclass_newline>, %*REG<tgt>, $i11),
                op('if_i', $i11, $fail),
                $donelabel
            ]);
        }
        elsif $subtype eq 'fail' {
            nqp::push(@ins, op('goto', $fail));
        }
        elsif $subtype eq 'pass' {
            # Nothing to do.
        }
        else {
            nqp::die("anchor subtype $subtype NYI");
        }
        release($i11, $MVM_reg_int64);
        @ins
    }

    my %cclass_code;
    INIT {
        %cclass_code<.>  := nqp::const::CCLASS_ANY;
        %cclass_code<d>  := nqp::const::CCLASS_NUMERIC;
        %cclass_code<s>  := nqp::const::CCLASS_WHITESPACE;
        %cclass_code<w>  := nqp::const::CCLASS_WORD;
        %cclass_code<n>  := nqp::const::CCLASS_NEWLINE;
    }

    method cclass($node) {
        my $subtype := $node.name;
        my $cclass := %cclass_code{ $subtype };
        self.panic("Unrecognized subtype '$subtype' in QAST::Regex cclass")
            unless $cclass;

        my @ins := nqp::list();
        my $i0 := fresh_i();
        nqp::push(@ins, op('ge_i', $i0, %*REG<pos>, %*REG<eos>));
        nqp::push(@ins, op('if_i', $i0, %*REG<fail>));

        if $cclass != nqp::const::CCLASS_ANY {
            my $testop := $node.negate ?? 'if_i' !! 'unless_i';
            nqp::push(@ins, op('const_i64', $i0, ival($cclass)));
            nqp::push(@ins, op('iscclass', $i0, $i0, %*REG<tgt>, %*REG<pos>));
            nqp::push(@ins, op($testop, $i0, %*REG<fail>));

            if $cclass == nqp::const::CCLASS_NEWLINE {
                my $s0 := fresh_s();
                nqp::push(@ins, op('const_s', $s0, sval("\r\n")));
                nqp::push(@ins, op('eqat_s', $i0, %*REG<tgt>, $s0, %*REG<pos>));
                nqp::push(@ins, op('add_i', %*REG<pos>, %*REG<pos>, $i0));
                release($s0, $MVM_reg_str);
            }
        }

        nqp::push(@ins, op('inc_i', %*REG<pos>));
        release($i0, $MVM_reg_int64);
        @ins
    }

    method concat($node) {
        my @ins := nqp::list();
        merge_ins(@ins, self.regex_mast($_)) for $node.list;
        @ins
    }

    method conj($node) { self.conjseq($node) }

    method conjseq($node) {
        my $prefix := $*QASTCOMPILER.unique($*RXPREFIX ~ '_rxconj');
        my $conjlabel_index := rxjump($prefix ~ '_fail');
        my $conjlabel := @*RXJUMPS[$conjlabel_index];
        my $firstlabel := label($prefix ~ '_first');
        my $iter := nqp::iterator($node.list);
        my @ins := [
            op('goto', $firstlabel),
            $conjlabel,
            op('goto', %*REG<fail>),
            # call the first child
            $firstlabel
        ];
        # make a mark that holds our starting position in the pos slot
        self.regex_mark(@ins, $conjlabel_index, %*REG<pos>, %*REG<zero>);
        merge_ins(@ins, self.regex_mast(nqp::shift($iter)));
        # use previous mark to make one with pos=start, rep=end
        my $i11 := fresh_i();
        my $i12 := fresh_i();
        self.regex_peek(@ins, $conjlabel_index, $i11);
        self.regex_mark(@ins, $conjlabel_index, $i11, %*REG<pos>);

        while $iter {
            nqp::push(@ins, op('set', %*REG<pos>, $i11));
            merge_ins(@ins, self.regex_mast(nqp::shift($iter)));
            self.regex_peek(@ins, $conjlabel_index, $i11, $i12);
            nqp::push(@ins, op('ne_i', $i12, %*REG<pos>, $i12));
            nqp::push(@ins, op('if_i', $i12, %*REG<fail>));
        }
        nqp::push(@ins, op('set', %*REG<pos>, $i11)) if $node.subtype eq 'zerowidth';
        release($i11, $MVM_reg_int64);
        release($i12, $MVM_reg_int64);
        @ins
    }

    method enumcharlist($node) {
        my @ins;
        if $node.negate {
            my $ok := label(self.unique($*RXPREFIX ~ '_enumcharlist'));
            nqp::push(@ins,
                op('indexat_scb', %*REG<tgt>, %*REG<pos>, sval($node[0]), $ok));
            nqp::push(@ins, op('goto', %*REG<fail>));
            nqp::push(@ins, $ok);
        }
        else {
            nqp::push(@ins,
                op('indexat_scb', %*REG<tgt>, %*REG<pos>, sval($node[0]), %*REG<fail>));
        }
        nqp::push(@ins, op('inc_i', %*REG<pos>))
            unless $node.subtype eq 'zerowidth';
        @ins
    }

    method literal($node) {
        my $litconst := $node[0];
        my $eq_op := $node.subtype eq 'ignorecase' ?? 'eqatic_s' !! 'eqat_s';
        my $s0 := fresh_s();
        my $i0 := fresh_i();
        my $cmpop := $node.negate ?? 'if_i' !! 'unless_i';
        my @ins := [
            label(self.unique($*RXPREFIX ~ '_literal')),
            # XXX create some regex prologue system so these const assignments
            # can happen only once at the beginning of a regex. hash of string constants
            # to the registers to which they are assigned.
            # XXX or make a specialized eqat_sc op that takes a constant string.
            op('const_s', $s0, sval($litconst)),
            # also, consider making the op branch directly from the comparison
            # instead of storing an integer to a temporary register
            op($eq_op, $i0, %*REG<tgt>, $s0, %*REG<pos>),
            op($cmpop, $i0, %*REG<fail>)
        ];
        unless $node.subtype eq 'zerowidth' {
            nqp::push(@ins, op('const_i64', $i0, ival(nqp::chars($litconst))));
            nqp::push(@ins, op('add_i', %*REG<pos>, %*REG<pos>, $i0));
        }
        release($s0, $MVM_reg_str);
        release($i0, $MVM_reg_int64);
        @ins
    }

    method pass($node) {
        my @ins := nqp::list();
        my @args := [%*REG<cur>, %*REG<pos>];
        my @flags := [$Arg::obj, $Arg::int];
        my $op;
        my $meth := fresh_o();
        release($meth, $MVM_reg_obj);
        nqp::push(@ins, op('findmeth', $meth, %*REG<cur>, sval('!cursor_pass')));
        if $node.name {
            my $sname := fresh_s();
            nqp::push(@ins, op('const_s', $sname, sval($node.name)));
            nqp::push(@args, $sname);
            nqp::push(@flags, $Arg::str);
        }
        if $node.backtrack ne 'r' {
            nqp::push(@args, sval('backtrack'));
            nqp::push(@args, %*REG<one>);
            nqp::push(@flags, $Arg::named +| $Arg::int);
        }
        nqp::push(@ins, call($meth, @flags, :result($meth), |@args));
        nqp::push(@ins, op('return_o', %*REG<cur>));
        @ins
    }

    sub resolve_condition_op($kind, $negated) {
        return $negated ??
            $kind == $MVM_reg_int64 ?? 'unless_i' !!
            $kind == $MVM_reg_num64 ?? 'unless_n' !!
            $kind == $MVM_reg_str   ?? 'unless_s' !!
            $kind == $MVM_reg_obj   ?? 'unless_o' !!
            ''
         !! $kind == $MVM_reg_int64 ?? 'if_i' !!
            $kind == $MVM_reg_num64 ?? 'if_n' !!
            $kind == $MVM_reg_str   ?? 'if_s' !!
            $kind == $MVM_reg_obj   ?? 'if_o' !!
            ''
    }

    method qastnode($node) {
        my @ins := [
            op('bindattr_i', %*REG<cur>, %*REG<curclass>, sval('$!pos'), %*REG<pos>, ival(-1)),
            op('bindlex', $*BLOCK.resolve_lexical('$¢'), %*REG<cur>)
        ];
        my $cmast := $*QASTCOMPILER.as_mast($node[0]);
        merge_ins(@ins, $cmast.instructions);
        release($cmast.result_reg, $cmast.result_kind);
        my $cndop := resolve_condition_op($cmast.result_kind, !$node.negate);
        if $node.subtype eq 'zerowidth' && $cndop ne '' {
            nqp::push(@ins, op($cndop, $cmast.result_reg, %*REG<fail>));
        }
        @ins
    }

    method quant($node) {
        my @ins := nqp::list();
        my $backtrack := $node.backtrack || 'g';
        my $sep       := $node[1];
        my $prefix    := self.unique($*RXPREFIX ~ '_rxquant_' ~ $backtrack);
        my $looplabel_index := rxjump($prefix ~ '_loop');
        my $looplabel := @*RXJUMPS[$looplabel_index];
        my $donelabel_index := rxjump($prefix ~ '_done');
        my $donelabel := @*RXJUMPS[$donelabel_index];
        my $min       := $node.min;
        my $max       := $node.max;
        my $needrep   := $min > 1 || $max > 1;
        my $needmark  := $needrep || $backtrack eq 'r';
        my $rep       := %*REG<rep>;
        my $pos       := %*REG<pos>;
        my $minreg := fresh_i();
        my $maxreg := fresh_i();
        nqp::push(@ins, op('const_i64', $minreg, ival($min))) if $min > 1;
        nqp::push(@ins, op('const_i64', $maxreg, ival($max))) if $max > 1;
        my $ireg := fresh_i();

        if $min == 0 && $max == 0 {
            # Nothing to do, and nothing to backtrack into.
        }
        elsif $backtrack eq 'f' {
            my $seplabel := label($prefix ~ '_sep');
            nqp::push(@ins, op('set', $rep, %*REG<zero>));
            if $min < 1 {
                self.regex_mark(@ins, $looplabel_index, $pos, $rep);
                nqp::push(@ins, op('goto', $donelabel));
            }
            nqp::push(@ins, op('goto', $seplabel)) if $sep;
            nqp::push(@ins, $looplabel);
            nqp::push(@ins, op('set', $ireg, $rep));
            if $sep {
                merge_ins(@ins, self.regex_mast($sep));
                nqp::push(@ins, $seplabel);
            }
            merge_ins(@ins, self.regex_mast($node[0]));
            nqp::push(@ins, op('set', $rep, $ireg));
            nqp::push(@ins, op('inc_i', $rep));
            if $min > 1 {
                nqp::push(@ins, op('lt_i', $ireg, $rep, $minreg));
                nqp::push(@ins, op('if_i', $ireg, $looplabel));
            }
            if $max > 1 {
                nqp::push(@ins, op('ge_i', $ireg, $rep, $maxreg));
                nqp::push(@ins, op('if_i', $ireg, $donelabel));
            }
            self.regex_mark(@ins, $looplabel_index, $pos, $rep) if $max != 1;
            nqp::push(@ins, $donelabel);
        }
        else {
            if $min == 0 { self.regex_mark(@ins, $donelabel_index, $pos, %*REG<zero>); }
            elsif $needmark { self.regex_mark(@ins, $donelabel_index, %*REG<negone>, %*REG<zero>); }
            nqp::push(@ins, $looplabel);
            merge_ins(@ins, self.regex_mast($node[0]));
            if $needmark {
                self.regex_peek(@ins, $donelabel_index, MAST::Local.new(:index(-1)), $rep);
                self.regex_commit(@ins, $donelabel_index) if $backtrack eq 'r';
                nqp::push(@ins, op('inc_i', $rep));
                if $max > 1 {
                    nqp::push(@ins, op('ge_i', $ireg, $rep, $maxreg));
                    nqp::push(@ins, op('if_i', $ireg, $donelabel));
                }
            }
            unless $max == 1 {
                self.regex_mark(@ins, $donelabel_index, $pos, $rep);
                merge_ins(@ins, self.regex_mast($sep)) if $sep;
                nqp::push(@ins, op('goto', $looplabel));
            }
            nqp::push(@ins, $donelabel);
            if $min > 1 {
                nqp::push(@ins, op('lt_i', $ireg, $rep, $minreg));
                nqp::push(@ins, op('if_i', $ireg, %*REG<fail>));
            }
        }
        release($ireg, $MVM_reg_int64);
        release($minreg, $MVM_reg_int64);
        release($maxreg, $MVM_reg_int64);
        @ins
    }

    method scan($node) {
        my $prefix := self.unique($*RXPREFIX ~ '_rxscan');
        my $looplabel_index := rxjump($prefix ~ '_loop');
        my $looplabel := @*RXJUMPS[$looplabel_index];
        my $scanlabel := label($prefix ~ '_scan');
        my $donelabel := label($prefix ~ '_done');
        my $ireg0 := fresh_i();
        my @ins := [
            op('getattr_i', $ireg0, %*REG<self>, %*REG<curclass>, sval('$!from'), ival(-1)),
            op('ne_i', $ireg0, $ireg0, %*REG<negone>),
            op('if_i', $ireg0, $donelabel),
            op('goto', $scanlabel),
            $looplabel,
            op('inc_i', %*REG<pos>),
            op('gt_i', $ireg0, %*REG<pos>, %*REG<eos>),
            op('if_i', $ireg0, %*REG<fail>),
            op('bindattr_i', %*REG<cur>, %*REG<curclass>, sval('$!from'), %*REG<pos>, ival(-1)),
            $scanlabel
        ];
        self.regex_mark(@ins, $looplabel_index, %*REG<pos>, %*REG<zero>);
        nqp::push(@ins, $donelabel);
        @ins
    }

    method subcapture($node) {
        my @ins := nqp::list();
        my $prefix := self.unique($*RXPREFIX ~ '_rxcap');
        my $donelabel := label($prefix ~ '_done');
        my $faillabel_index := rxjump($prefix ~ '_fail');
        my $faillabel := @*RXJUMPS[$faillabel_index];
        my $i11 := fresh_i();
        my $p11 := fresh_o();
        my $s11 := fresh_s();
        self.regex_mark(@ins, $faillabel_index, %*REG<pos>, %*REG<zero>);
        merge_ins(@ins, self.regex_mast($node[0]));
        self.regex_peek(@ins, $faillabel_index, $i11);
        merge_ins(@ins, [
            op('findmeth', %*REG<method>, %*REG<cur>, sval('!cursor_start_subcapture')),
            call(%*REG<method>, [$Arg::obj, $Arg::int], %*REG<cur>, $i11, :result($p11)),
            op('findmeth', %*REG<method>, $p11, sval('!cursor_pass')),
            call(%*REG<method>, [$Arg::obj, $Arg::int], $p11, %*REG<pos>),
            op('findmeth', %*REG<method>, %*REG<cur>, sval('!cursor_capture')),
            op('const_s', $s11, sval($node.name)),
            call(%*REG<method>, [$Arg::obj, $Arg::obj, $Arg::str],
                %*REG<cur>, $p11, $s11, :result(%*REG<cstack>)),
            op('goto', $donelabel),
            $faillabel,
            op('goto', %*REG<fail>),
            $donelabel
        ]);
        release($i11, $MVM_reg_int64);
        release($p11, $MVM_reg_obj);
        release($s11, $MVM_reg_str);
        @ins
    }

    my @kind_to_args := [0,
        $Arg::int,  # $MVM_reg_int8            := 1;
        $Arg::int,  # $MVM_reg_int16           := 2;
        $Arg::int,  # $MVM_reg_int32           := 3;
        $Arg::int,  # $MVM_reg_int64           := 4;
        $Arg::num,  # $MVM_reg_num32           := 5;
        $Arg::num,  # $MVM_reg_num64           := 6;
        $Arg::str,  # $MVM_reg_str             := 7;
        $Arg::obj   # $MVM_reg_obj             := 8;
    ];

    method subrule($node) {
        my @ins      := nqp::list();
        my $subtype  := $node.subtype;
        my $testop   := $node.negate ?? 'ge_i' !! 'lt_i';
        my $captured := 0;

        my $cpn := self.children($node[0]);
        my @pargs := $cpn[1];
        my @pkinds := $cpn[2]; # positional result registers

        my $submast := nqp::shift(@pargs);
        my $submast_kind := nqp::shift(@pkinds);
        release($submast, $submast_kind);

        my $i := 0;
        my @flags := [$Arg::obj];
        for @pkinds {
            nqp::push(@flags, @kind_to_args[$_]);
            release(@pargs[$i++], $_);
        }

        my $p11 := %*REG<P11>;
        my $i11 := fresh_i();

        merge_ins(@ins, $cpn[0]);
        merge_ins(@ins, [
            op('bindattr_i', %*REG<cur>, %*REG<curclass>, sval('$!pos'),
                %*REG<pos>, ival(-1))
        ]);
        if nqp::istype($node[0][0], QAST::SVal) {
            # Method call.
            merge_ins(@ins, [
                op('findmeth', %*REG<method>, %*REG<cur>, sval($node[0][0].value)),
                call(%*REG<method>, @flags, %*REG<cur>, |@pargs, :result($p11))
            ]);
        }
        else {
            # Normal invocation (probably positional capture).
            merge_ins(@ins, [
                call($submast, @flags, %*REG<cur>, |@pargs, :result($p11))
            ]);
        }
        merge_ins(@ins, [
            op('getattr_i', $i11, $p11, %*REG<curclass>, sval('$!pos'), ival(-1)),
            op($testop, $i11, $i11, %*REG<zero>),
            op('if_i', $i11, %*REG<fail>)
        ]);

        if $subtype ne 'zerowidth' {
            my $rxname := self.unique($*RXPREFIX ~ '_rxsubrule');
            my $passlabel_index := rxjump($rxname ~ '_pass');
            my $passlabel := @*RXJUMPS[$passlabel_index];
            if $node.backtrack eq 'r' {
                unless $subtype eq 'method' {
                    self.regex_mark(@ins, $passlabel_index, %*REG<negone>, %*REG<zero>);
                }
                nqp::push(@ins, $passlabel);
            }
            else {
                my $backlabel_index := rxjump($rxname ~ '_back');
                my $backlabel := @*RXJUMPS[$backlabel_index];
                merge_ins(@ins, [
                    op('goto', $passlabel),
                    $backlabel,
                    # %*REG<P11> ($p11 here) is magically set just before the jump at the backtracker
                    op('findmeth', %*REG<method>, $p11, sval('!cursor_next')),
                    call(%*REG<method>, [$Arg::obj], $p11, :result($p11)),
                    op('getattr_i', $i11, $p11, %*REG<curclass>, sval('$!pos'), ival(-1)),
                    op($testop, $i11, $i11, %*REG<zero>),
                    op('if_i', $i11, %*REG<fail>),
                    $passlabel
                ]);

                if $subtype eq 'capture' {
                    my $sname := fresh_s();
                    nqp::push(@ins, op('findmeth', %*REG<method>, %*REG<cur>,
                        sval('!cursor_capture')));
                    nqp::push(@ins, op('const_s', $sname, sval($node.name)));
                    nqp::push(@ins, call(%*REG<method>, [$Arg::obj, $Arg::obj, $Arg::str],
                        %*REG<cur>, $p11, $sname, :result(%*REG<cstack>)));
                    release($sname, $MVM_reg_str);
                    $captured := 1;
                }
                else {
                    nqp::push(@ins, op('findmeth', %*REG<method>, %*REG<cur>,
                        sval('!cursor_push_cstack')));
                    nqp::push(@ins, call(%*REG<method>, [$Arg::obj, $Arg::obj],
                        %*REG<cur>, $p11, :result(%*REG<cstack>)));
                }

                my $bstack := %*REG<bstack>;
                merge_ins(@ins, [
                    op('const_i64', $i11, ival($backlabel_index)),
                    op('push_i', $bstack, $i11),
                    op('push_i', $bstack, %*REG<zero>),
                    op('push_i', $bstack, %*REG<pos>),
                    op('elems', $i11, %*REG<cstack>),
                    op('push_i', $bstack, $i11)
                ]);
            }
        }

        if !$captured && $subtype eq 'capture' {
            my $sname := fresh_s();
            nqp::push(@ins, op('findmeth', %*REG<method>, %*REG<cur>,
                sval('!cursor_capture')));
            nqp::push(@ins, op('const_s', $sname, sval($node.name)));
            nqp::push(@ins, call(%*REG<method>, [$Arg::obj, $Arg::obj, $Arg::str],
                %*REG<cur>, $p11, $sname, :result(%*REG<cstack>)));
            release($sname, $MVM_reg_str);
        }

        nqp::push(@ins, op('getattr_i', %*REG<pos>, $p11, %*REG<curclass>,
            sval('$!pos'), ival(-1))) unless $subtype eq 'zerowidth';

        release($i11, $MVM_reg_int64);

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
            op('const_i64', $mark, ival($label_index)),
            op('elems', $elems, $bstack),
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
            op('const_i64', $mark, ival($label_index)),
            op('elems', $ptr, $bstack),
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
            nqp::push(@ins, op('atpos_i', $_, $bstack, $ptr)) if $_.index != -1;
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
            op('const_i64', $mark, ival($label_index)),
            op('elems', $ptr, $bstack),
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
            op('if_i', $i0, $makemarklabel),
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
        unless $node ~~ QAST::Regex {
            my $mast := $*QASTCOMPILER.as_mast($node);
            release($mast.result_reg, $mast.result_kind);
            return $mast.instructions;
        }
        my $rxtype := $node.rxtype() || 'concat';
        self."$rxtype"($node) # expects to return an nqp::list of instructions
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

    sub call($target, @flags, :$result?, *@args) {
        nqp::defined($result) ??
        MAST::Call.new(
            :target($target), :result($result), :flags(@flags), |@args
        ) !!
        MAST::Call.new(
            :target($target), :flags(@flags), |@args
        )
    }

    sub releasei($ilist) { release($ilist.result_reg, $ilist.result_kind) }
    sub release($reg, $type) { $*REGALLOC.release_register($reg, $type) }

    sub fresh_i() { $*REGALLOC.fresh_i() }
    sub fresh_n() { $*REGALLOC.fresh_n() }
    sub fresh_s() { $*REGALLOC.fresh_s() }
    sub fresh_o() { $*REGALLOC.fresh_o() }

    sub label($name) { MAST::Label.new( :name($name) ) }
    sub ival($val) { MAST::IVal.new( :value($val) ) }
    sub nval($val) { MAST::NVal.new( :value($val) ) }
    sub sval($val) { MAST::SVal.new( :value($val) ) }

}
