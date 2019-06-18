use Data::Dump::Tree;

#my $testprog = q:to/CONFPROG/;
    #version = 1
    #entry profiler_static:
    #profile |= sf.name eq "florble";
    #profile |= sf.name eq "blorb";
    #$callerframe = frame.caller.static_info
    #$custom_var = (sf.name eq "blurb") and ($callerframe.name eq "blop")
    #profile |= $custom_var
    #log = "oh no!";
    #CONFPROG

my $testprog = q:to/CONFPROG/;
    version = 1
    entry profiler_static:
    log = "static profiler entrypoint";
    log = filename(sf);
    profile = 1 + ((contains(filename(sf), "/core/")) * 3);
    CONFPROG

die "only support version 1" unless $testprog.lines.head ~~ m/^version \s+ "=" \s+ 1 \s* ";"? $/;

class CPType { ... }

my \CPInt    = CPType.new(name => "Int", :numeric, :stringy);
my \CPString = CPType.new(name => "String", :stringy);
my \CString  = CPType.new(name => "CString", :stringy);

my \MVMStaticFrame = CPType.new(name => "MVMStaticFrame",
    attributes => {
        cu            => "MVMCompUnit",
        env_size      => CPInt,
        work_size     => CPInt,
        num_lexicals  => CPInt,
        cuuid         => CPString,
        name          => CPString,
        outer         => "MVMStaticFrame",
    });

my \MVMCompUnit = CPType.new(name => "MVMCompUnit",
    attributes => {
        hll_name     => CPString,
        filename     => CPString,
    });

my \MVMFrame = CPType.new(name => "MVMFrame",
    attributes => {
        outer        => "MVMFrame",
        caller       => "MVMFrame",
        params       => "MVMArgProcContext",
        return_type  => CPInt,
        static_info  => MVMStaticFrame,
    });

my \MVMThreadContext = CPType.new(name => "MVMThreadContext",
    attributes => {
        thread_id   => CPInt,
        num_locks   => CPInt,
        cur_frame   => MVMFrame,
    });


grammar ConfProg {
    regex TOP {
        "version" \s* "=" \s* "1" \n
        <statement>+ %% <.eol>
    }

    regex eol { \s* ';' \n* | \s* \n+ | \s* \n* $$ }

    proto regex statement { * }

    regex statement:<var_update> {
        <variable> \s+ <update_op> \s+ <expression>
    }
    regex statement:<entrypoint> {
        'entry' \s+ $<entrypoint>=[
            | 'profiler_static'
            | 'profiler_dynamic'
            | 'spesh'
            | 'jit'
            | 'heapsnapshot'
        ] \s*
        ":"
    }

    regex statement:<continue> {
        'continue'
    }

    regex statement:<label> {
        <ident> \s* ':'
    }

    proto regex variable { * }

    regex variable:<custom> {
        '$' <.ident>
    }
    regex variable:<builtin> {
        <.ident>
    }

    proto regex update_op { * }

    regex update_op:<logical> {
        '|=' | '&='
    }

    regex update_op:<assignment> {
        '='
    }

    proto regex expression { * }

    proto regex one_expression { * }

    regex one_expression:<literal_number> {
        <[1..9]> '_'? <[0..9]>* % '_'? | 0
    }
    regex one_expression:<literal_number_base16> {
        "0x" [<[1..9a..fA..F]> '_'? <[0..9a..fA..F]>* % '_'? | 0]
    }

    regex one_expression:<literal_string> {
        | '"' [ <-["]> | \\ \" ]+ '"'
        | "'" [ <-[']> | \\ \' ]+ "'"
    }

    regex one_expression:<drilldown> {
        <variable> <postfixish>+
    }

    regex one_expression:<variable> {
        <variable>
    }

    regex expression:<one> {
         <one_expression> [\s* [<compop>|<arithop>] \s* <other_expression=.one_expression>]?
    }

    regex one_expression:<parenthesized> {
        '(' \s* <expression> \s* ')'
    }
    regex one_expression:<prefixed> {
        <prefixop> '(' \s* <expression> \s* ')'
    }

    regex one_expression:<functioncall> {
        <ident> '(' <one_expression>* %% [\s* ',' \s*] ')'
    }

    proto regex postfixish { * }
    regex postfixish:<attribute> {
        '.' <ident>
    }
    regex postfixish:<positional> {
        '.' "[" <.one_expression> "]"
    }

    regex compop {
        [
        | "eq"
        | "ne"
        | '&&'
        | '||'
        | "and"
        | "or"
        ]
    }

    regex arithop {
        | "+"
        | "-"
        | "*"
        | "/"
    }

    regex prefixop {
        [
        | '!'
        | '+'
        | '~'
        ]
    }
}

class Node {
    has @.children;
}

class Op is Node {
    has Str $.op;
    has $.type is rw;
}
class Var is Node {
    has $.name;
    has $.scope;
    has $.type is rw;

    method ddt_get_elements {
        ('$.name', " = ", $.name),
        (('$.type', " = ", $.type) if $.type)
    }
}
class SVal is Node {
    has Str $.value;

    method ddt_get_header { "String Value ($.value.perl())" }
    method ddt_get_elements { [] }

    method type { CPString }
}
class IVal is Node {
    has Int $.value;

    method ddt_get_header { "Int Value ($.value)" }

    method ddt_get_elements { [] }

    method type { CPInt }
}
class Label is Node {
    has $.name;
    has $.type;
    has $.position is rw; # stores bytecode offset during generation
}

my %op-to-op = <
    =  bind
    == eq_i
    != ne_i
    eq eq_s
    ne ne_s
    && and_i
    || or_i
    and and_i
    or or_i
    + add_i
    - sub_i
    * mul_i
    / div_i
>;

my %prefix-to-op = <
    ~ stringify
    + intify
    ! negate
>;

class ConfProgActions {
    has %.labels;

    method prefixop($/) { make $/.Str }
    method compop($/)   { say $/.Str; make %op-to-op{$/.Str} }
    method arithop($/)   { say $/.Str; make %op-to-op{$/.Str} }

    method variable:<custom>($/)    { make Var.new( name => $/.Str, scope => "my") }
    method variable:<builtin>($/)   { make Var.new( name => $/.Str, scope => "builtin") }

    method update_op:<logical>($/)    { make $/.Str }
    method update_op:<assignment>($/) { make $/.Str }

    method one_expression:<literal_number>($/) { make IVal.new(value => $/.Str.Int) }
    method one_expression:<literal_number_base16>($/) { make IVal.new(value => $/.Str.Int) }

    method one_expression:<literal_string>($/) {
        die "only very literal strings are allowed; you must not use \\q or \\qq." if $/.Str.contains("\\q");
        use MONKEY-SEE-NO-EVAL;
        # force single-quote semantics
        make SVal.new( value => ("q" ~ $/.Str).&EVAL );
    }

    method postfixish:<attribute>($/) {
        make Op.new( op => "getattr", children => [Any, $<ident>.Str] )
    }
    method postfixish:<positional>($/) {
        make Op.new( op => "getattr", children => [Any, $<one_expression>.ast] )
    }

    method expression:<one>($/) {
        if $<other_expression> && ($<compop> || $<arithop>) {
            make Op.new( op => ($<compop> || $<arithop>).ast,
                children => [
                    $<one_expression>.ast,
                    $<other_expression>.ast,
                ] );
        }
        else {
            make $<one_expression>.ast
        }
    }

    method one_expression:<variable>($/) { make $<variable>.ast }
    method one_expression:<parenthesized>($/) { make $<expression>.ast }

    method one_expression:<prefixed>($/) {
        make Op.new(
            op => %prefix-to-op{$<prefixop>.Str},
            children => [
                $<expression>.ast
            ]);
    }

    method one_expression:<drilldown>($/) {
        my $result = $<variable>.ast;
        for $<postfixish>>>.ast {
            $_.children[0] = $result;
            $result = $_;
        }
        make $result;
    }

    method one_expression:<functioncall>($/) {
        use Data::Dump::Tree::ExtraRoles;

        my @positionals;
        my @nameds;
        #my $matchddt = Data::Dump::Tree.new;
        #$matchddt does DDTR::MatchDetails;
        #$matchddt.dump: $/;

        for @<one_expression> {
            @positionals.push: .ast;
            #orwith .<named> {
                #die "named arguments in functioncall NYI";
                #@nameds.push: .ast;
            #}
        }

        my $result = Op.new(
            op => "call",
            children => [
                flat $<ident>.Str,
                @positionals, @nameds
            ]);
        make $result;
    }

    method statement:<entrypoint>($/) {
        make Label.new(
            name => $<entrypoint>.Str,
            type => "entrypoint"
        );
    }
    method statement:<label>($/) {
        my $label = Label.new(
            name => $<ident>.Str,
            type => "user"
        );
        with %.labels{$<ident>.Str} {
            die "duplicate definition of label $<ident>.Str()";
        }
        else {
            $_ = $label;
        }
        make $label;
    }

    method statement:<var_update>($/) {
        make Op.new(
            op => $<update_op>.ast,
            children => [
                $<variable>.ast,
                $<expression>.ast,
            ]
        )
    }

    method TOP($/) { make $<statement>>>.ast }
}

my %typesystem;

class CustomDDTOutput {
    has $.ddt_get_header;
    has $.ddt_get_elements;
}

class CPType {
    my %typesystem-fixups{Any};

    has Str $.name;

    has %.attributes;
    has CPType $.positional;
    has CPType $.associative;

    has Bool $.numeric;
    has Bool $.stringy;

    method ddt_get_header {
        "CPType $.name()$( " :numeric" if $.numeric )$( " :stringy" if $.stringy ) "
    }
    method ddt_get_elements {
        []
    }

    method TWEAK {
        %typesystem{$.name} = self;
        for %!attributes -> $p {
            if $p.value ~~ Str {
                with %typesystem{$p.value} {
                    $p.value = $_
                }
                else {
                    %typesystem-fixups{$p.value}.push: (self, $p.key);
                }
            }
        }
        for %typesystem-fixups{self.name}:v {
            .[0].attributes{.[1]} = self;
        }
    }
}

# XXX not all builtins are valid after all entrypoints
# XXX some code can be for two entrypoints at once, so unify types if necessa
my %builtins = %(
    sf => MVMStaticFrame,
    frame => MVMFrame
);

my %targets = %(
    profile => CPInt,
    log => CPString,
    snapshot => CPInt,
);

use MASTOps:from<NQP>;

my %original-op-gen := MAST::Ops.WHO<%generators>;

my %op-gen = %(
    do for %original-op-gen.keys {
        $_ => -> |c { %original-op-gen{$_}(|c); $*MAST_FRAME.dump-new-stuff() }
    }
);

my %entrypoint-indices = <
    profiler_static
    profiler_dynamic
    spesh
    jit
    heapsnapshot
>.antipairs;

my $*MAST_FRAME = class {
    has @.bytecode is buf8;
    has str @.strings;
    has @.entrypoints is default(1);

    has %.labelrefs{Any};

    has $.dump-position = 0;

    method add-string(str $str) {
        my $found := @!strings.grep($str, :k);
        if $found -> $_ {
            return $_[0]
        }
        @!strings.push: $str;
        @!strings.end;
    }
    method add-entrypoint(str $name) {
        with %entrypoint-indices{$name} {
            given @!entrypoints[$_] {
                when 1 {
                    $_ = @.bytecode.elems;
                }
                default {
                    die "duplicate entrypoint $name";
                }
            }
        }
        else {
            die "unknown entrypoint $name";
        }
    }
    method compile_label($bytecode, $label) {
        with $label.position {
            die "labels must not appear before their declaration, sorry!"
        }
        %.labelrefs{$label}.push: $bytecode.elems;
        $bytecode.write-uint32($bytecode.elems, 0xbbaabbaabbaabba);
    }

    method finish-labels() {
        for %.labelrefs {
            say "key and value:";
            say .key.perl();
            say .value.perl();
            for .value.list -> $fixup-pos {
                @!bytecode.write-uint32($fixup-pos, .key.position);
            }
        }
    }

    method dump-new-stuff() {
        for @!bytecode.skip($!dump-position) -> $a, $b {
            state $pos = 0;
            my int $cur-pos = $!dump-position + $pos;
            $pos += 2;

            my int $sixteenbitval = $b * 256 + $a;

            if $pos == 2 {
                use nqp;

                my Mu $names := MAST::Ops.WHO<@names>;
                if $sixteenbitval < nqp::elems($names) {
                    say nqp::atpos_s($names, $sixteenbitval);
                }
            }

            say "$cur-pos.fmt("0x% 4x") $sixteenbitval.fmt("%04x") ($sixteenbitval.fmt("%05d"))  -- $a.fmt("%02x") $b.fmt("%02x") / $a.fmt("%03d") $b.fmt("%03d")";
        }
        $!dump-position = +@!bytecode;
        say "";
    }
}.new;

my $parseresult = ConfProg.parse($testprog, actions => ConfProgActions.new);

#"$_[1].perl()".say && $_[0].&ddt && "".say for $parseresult.ast Z $parseresult<statement>.list>>.Str;

# type derivation step

multi sub unify_type(Op $node) {
    given $node.op {
        when "getattr" {
            my $source = $node.children[0];
            my $attribute = $node.children[1];
            unify_type($source) unless defined $source.type;
            die "how did you get a getattr node with a non-string attribute name?" unless $attribute ~~ Str;
            die "don't have a type for this" unless $source.type ~~ CPType:D;
            with $source.type.attributes{$attribute} {
                $node.type = $_;
                return $_;
            }
            else {
                die "type $source.type.name() doesn't have an attribute $attribute" 
            }
        }
        when "|=" | "&=" {
            my $source = $node.children[1];
            unify_type($source) unless defined $source.type;
            die "can only use $node.op() with something that can intify" unless defined $source.type && $source.type.numeric;
            $node.type = CPInt;
        }
        when any(<eq_s ne_s>) {
            my $lhs = $node.children[0];
            my $rhs = $node.children[1];
            for $lhs, $rhs {
                .&unify_type() unless defined .type;
            }
            die "lhs of string op must be stringy" unless $lhs.type.stringy;
            die "rhs of string op must be stringy" unless $rhs.type.stringy;
            $node.type = CPInt;
        }
        when any(<and_i or_i add_i sub_i mul_i div_i>) {
            my $lhs = $node.children[0];
            my $rhs = $node.children[1];

            $lhs.&unify_type without $lhs.type;
            $rhs.&unify_type without $rhs.type;

            die "arguments to $node.op() must be numericable" unless $lhs.type.numeric && $rhs.type.numeric;
            $node.type = CPInt;
        }
        when "=" {
            my $lhs = $node.children[0];
            my $rhs = $node.children[1];

            die "can only assign to vars, not { try $lhs.type.name() orelse $lhs.^name }" unless $lhs ~~ Var;

            $rhs.&unify_type() without $rhs.type;

            if $lhs.scope eq "my" {
                with %*LEXPAD{$lhs.name} {
                    #ddt $rhs;
                    die "cannot assign $rhs.type.name() to variable $lhs.name(), it was already typed to accept only $_.type.name()" unless $rhs.type eqv .type;
                }
                else {
                    %*LEXPAD{$lhs.name} = $rhs;
                }
            }
            elsif $lhs.scope eq "builtin" {
                with %targets{$lhs.name} {
                    $lhs.type = %targets{$lhs.name};
                    die "type mismatch assigning to output variable $lhs.name(); expected $_.name(), but got a $rhs.type.name()" unless $_ eqv $rhs.type;
                }
                else {
                    die "target $lhs.name() not known (did you mean to declare a custom variable \$$lhs.name()";
                }
            }
        }
        when "stringify" | "intify" {
            $node.children[0].&unify_type;

            $node.type =
                ($node.op eq "stringify"
                    ?? CPString
                    !! ($node.op eq "intify"
                        ?? CPInt
                        !! die "what"));
        }
        when "negate" {
            $node.children[0].&unify_type;

            die "negate only works on integers ATM. sorry." unless $node.children[0].type eqv CPInt;

            $node.type = CPInt;
        }
        when "call" {
            # Go by first child, it ought to be a string with the function name.
            my $funcname = $node.children[0];
            given $funcname {
                when "choice" {
                    $node.type = CPInt;
                }
                when "starts-with" | "ends-with" | "contains" | "index" {
                    $node.children[1].&unify_type() without $node.children[1].type;
                    $node.children[2].&unify_type() without $node.children[2].type;
                    $node.type = CPInt;
                    ddt $node.children;
                    die "$funcname only takes two arguments" unless
                        $node.children == 3;
                    die "$funcname requires two strings as arguments" unless $node.children[1&2].type eqv CPString;
                }
                when "filename" | "lineno" {
                    $node.children[1].&unify_type() without $node.children[1].type;

                    ddt $node;

                    die "$funcname requires a single argument" unless $node.children == 2;
                    die "$funcname requires a MVMStaticFrame, not a $node.children[1].type.name()" unless $node.children[1].type eqv MVMStaticFrame;

                    if $funcname eq "filename" {
                        $node.type = CPString;
                    }
                    else {
                        $node.type = CPInt;
                    }
                }
                default {
                    die "function call to $funcname.perl() NYI, typo'd, or something else is wrong";
                }
            }
        }
        default {
            warn "unhandled node op $node.op() in unify_type for an Op";
            return Any;
        }
    }
}
multi sub unify_type(Var $var) {
    with %builtins{$var.name} {
        $var.type = $_
    }
    else {
        with %*LEXPAD{$var.name} {
            $var.type = .type;
        }
        else {
            say "cannot figure out type of variable $var.name()";
        }
    }
}
multi sub unify_type(Any $_) {
    Any
}

my %*LEXPAD;
for $parseresult.ast {
    unify_type($_);
}
say "==========";
ddt $parseresult.ast;
say "";
say "lexpad";
say "";
ddt %*LEXPAD;

my $*REGALLOC = class {
    has @!types;
    has @!usage;

    submethod BUILD {
        @!types = [RegString, RegStruct, RegInteger];
        @!usage = [-1 xx $custom_reg_base];
    }

    multi method fresh(RegisterType $type) {
        #say "looking for a free register of type $type";
        #say 0..* Z @!types Z @!usage;
        for 0..* Z @!types Z @!usage {
            if .[1] eqv $type && .[2] == 0 {
                .[2] = 1;
                return .[0]
            }
        }
        @!types.push: $type;
        @!usage.push: 1;
        return @!usage.end;
    }
    multi method fresh(CPType $type) {
        self.fresh(to-reg-type($type));
    }
    method release($reg) {
        given @!usage[$reg] {
            when 1 {
                $_ = 0
            }
            when 0 {
                die "tried to double-release register $reg";
            }
            when -1 {
                die "tried to release special register $reg ($(SpecialRegister($reg)))"
            }
            when Any:U {
                die "tried to free register $reg, but only @!usage.elems() registers were allocated yet";
            }
            default {
                die "unexpected usage value $_.perl() for register $reg"
            }
        }
    }
}.new;

multi sub compile_coerce($node, $type, :$target!) {
    if $node.type eqv $type {
        my $rhstarget = $*REGALLOC.fresh($type);
        compile_node($node, :target($rhstarget));
        %op-gen<set>($target, $rhstarget);
        $*REGALLOC.release($rhstarget);
    }
    elsif $node.type eqv CPInt {
        if $type eqv CPString {
            my $rhstarget = $*REGALLOC.fresh(RegString);
            compile_node($node, :target($rhstarget));
            %op-gen<coerce_si>($target, $rhstarget);
            $*REGALLOC.release($rhstarget);
        }
        else {
            die "NYI coerce";
        }
    }
    else {
        die "unsupported coerce";
    }
}

my $current-entrypoint;

multi sub compile_node(SVal $val, :$target!) {
    note "const_s into $target";
    %op-gen<const_s>($target, $val.value);
}
multi sub compile_node(IVal $val, :$target!) {
    note "const_i into $target";
    %op-gen<const_i64>($target, $val.value);
}
multi sub compile_node(Var $var, :$target) {
    if $var.scope eq "builtin" {
        given $var.name {
            when "sf" {
                note "builtin var sf to $target (const_s, then getattr)";
                %op-gen<const_s>(0, "");
                %op-gen<getattr_o>($target, STRUCT_ACCUMULATOR, STRUCT_SELECT, "staticframe", 0);
            }
            default {
                die "builtin variable $var.name() NYI";
            }
        }
    }
    elsif $var.scope eq "my" {
        die "user-specified variables NYI"
    }
    else {
        die "unexpected variable scope $var.scope()";
    }
}

my int $dynamic_label_idx = 1;

sub compile_call(Op $op, :$target) {
    my $funcname = $op.children[0].Str;
    given $funcname {
        when "choice" {
            say "";
            say "here is the choice function!";
            say "";
            my $value = $*REGALLOC.fresh(RegNum);
            %op-gen<rand_n>($value);

            my $comparison-value = $*REGALLOC.fresh(RegNum);
            my $comparison-result = $*REGALLOC.fresh(RegInteger);

            my num $step = 1e0 / ($op.children.elems - 1);

            my @individual-labels = Label.new(name => "choice-" ~ $dynamic_label_idx++, type => "internal") xx ($op.children.elems - 2);
            my $end-label = Label.new(name => "choice-exit-" ~ $dynamic_label_idx++, type => "internal");
            @individual-labels.push: $end-label;

            my $current-comparison-literal = $step;

            ddt $op;

            for $op.children<>[1..*].list Z @individual-labels {
                say "choice loop: $_.perl()";
                say "$_[0].perl()";
                say "$_[1].perl()";
                %op-gen<const_n64>($comparison-value, $current-comparison-literal);
                %op-gen<gt_n>($comparison-result, $value, $comparison-value);
                %op-gen<if_i>($comparison-result, .[1]);
                compile_node(.[0], :$target);
                %op-gen<goto>($end-label);
                compile_node(.[1]);
            }
            compile_node($end-label);
        }
        when "starts-with" | "ends-with" | "contains" | "index" {
            my $haystackreg = $*REGALLOC.fresh(RegString);

            compile_node($op.children[1], target => $haystackreg);

            my $needlereg   = $*REGALLOC.fresh(RegString);

            compile_node($op.children[2], target => $needlereg);

            #my $resultreg = $*REGALLOC.fresh(RegInteger);
            my $resultreg = $target;

            if $funcname eq "contains" | "index" {
                my $positionreg = $*REGALLOC.fresh(RegInteger);
                %op-gen<const_i64_16>($positionreg, 0);
                %op-gen<index_s>($resultreg, $haystackreg, $needlereg, $positionreg);
                # index_s returns -1 on not found, 0 or higher on "found in some position"
                # so we increment by 1 to get zero vs nonzero
                %op-gen<const_i64_16>($positionreg, 1);
                %op-gen<add_i>($resultreg, $resultreg, $positionreg);
                # to get an actual bool result, do the old C trick of
                # negating the value twice
                %op-gen<not_i>($resultreg, $resultreg);
                %op-gen<not_i>($resultreg, $resultreg);
                $*REGALLOC.release($positionreg);
            }
            else {
                my $positionreg = $*REGALLOC.fresh(RegInteger);
                if $funcname eq "starts-with" {
                    %op-gen<const_i64_16>($positionreg, 0);
                }
                elsif $funcname eq "ends-with" {
                    %op-gen<chars>($positionreg, $haystackreg);
                    my $needlelenreg = $*REGALLOC.fresh(RegInteger);
                    %op-gen<chars>($needlelenreg, $needlereg);
                    %op-gen<sub_i>($positionreg, $positionreg, $needlelenreg);
                    $*REGALLOC.release($needlelenreg);
                }
                %op-gen<eqat_s>($resultreg, $haystackreg, $needlereg, $positionreg);
                $*REGALLOC.release($positionreg);
            }

            $*REGALLOC.release($haystackreg);
            $*REGALLOC.release($needlereg);
        }
        when "filename" | "lineno" {
            compile_node($op.children[1], target => STRUCT_ACCUMULATOR);

            %op-gen<getcodelocation>(STRUCT_ACCUMULATOR, STRUCT_ACCUMULATOR);

            my $func = $funcname eq "filename"
                ?? %op-gen<smrt_strify>
                !! %op-gen<smrt_intify>;
            $func($target, STRUCT_ACCUMULATOR);
        }
        default {
            die "Cannot compile call of function $funcname yet"
        }
    }
}

multi sub compile_node(Op $op, :$target) {
    note "Op $op.op() into $($target // "-")";
    given $op.op {
        when "=" {
            my $lhs = $op.children[0];
            my $rhs = $op.children[1];

            my $rhstarget = $*REGALLOC.fresh($lhs.type);
            compile_node($rhs, target => $rhstarget);

            if $lhs.scope eq "builtin" {
                if $lhs.name eq "log" {
                    %op-gen<say>($rhstarget);
                }
                elsif $lhs.name eq "snapshot" | "profile" {
                    note "making a set op into FEATURE_TOGGLE from $rhstarget";
                    %op-gen<set>(FEATURE_TOGGLE, $rhstarget);
                }
                else {
                    die "builtin variable $lhs.name() NYI";
                }
            }
            else {
                die "variable scope $lhs.scope() NYI";
            }
            $*REGALLOC.release($rhstarget);
        }
        when "stringify" {
            my $child = $op.children[0];

            my $valtarget = $*REGALLOC.fresh(RegCString);

            compile_node($child, target => $valtarget);
            %op-gen<smrt_strify>($target, $valtarget);
            $*REGALLOC.release($valtarget);
        }
        when "intify" {
            my $child = $op.children[0];

            my $valtarget = $*REGALLOC.fresh(RegInteger);

            compile_node($child, target => $valtarget);
            %op-gen<smrt_intify>($target, $valtarget);
            $*REGALLOC.release($valtarget);
        }
        when "getattr" {
            my $value = $op.children[0];
            my $attribute = $op.children[1];

            my $targetreg;
            my $targetregtype = to-reg-type($value.type);

            if $targetregtype eqv RegStruct {
                $targetreg = STRUCT_ACCUMULATOR
            }
            else {
                $targetreg = $*REGALLOC.fresh($targetregtype)
            }

            compile_node($value, target => $targetreg);

            # select right struct type
            # this ends up as two noops after the validator has
            # seen it, but the getattr that comes next will use
            # the type identified here for the struct type.
            note "const_s in here for STRUCT_SELECT for $value.type.name()";
            %op-gen<const_s>(STRUCT_SELECT, $value.type.name);

            note "getattr assigning from $targetreg to $targetreg";
            %op-gen<getattr_o>($target, $targetreg, STRUCT_SELECT, $attribute, 0);

            if $targetreg != STRUCT_ACCUMULATOR {
                $*REGALLOC.release($targetreg)
            }
        }
        when any(<eq_s ne_s add_i sub_i mul_i div_i>)  {
            my $lhs = $op.children[0];
            my $rhs = $op.children[1];

            my $leftreg = $*REGALLOC.fresh(RegString);
            compile_node($lhs, target => $leftreg);

            my $rightreg = $*REGALLOC.fresh(RegString);
            compile_node($rhs, target => $rightreg);

            note "putting a $op.op() in here between $leftreg and $rightreg";
            %op-gen{$op.op()}($target, $leftreg, $rightreg);

            $*REGALLOC.release($leftreg);
            $*REGALLOC.release($rightreg);
        }
        when "call" {
            compile_call($op, :$target) with $target;
            compile_call($op) without $target;
        }
        when "negate" {
            compile_node($op.children[0], :$target);
            %op-gen<not_i>($target, $target);
        }
        default {
            die "cannot compile $op.op() yet";
        }
    }
}
multi compile_node(Label $label) {
    given $label.type {
        when "user" | "internal" {
            say "compiling label node and setting position:";
            $label.position = $*MAST_FRAME.bytecode.elems;
            say "    $label.position.perl()";
        }
        when "entrypoint" {
            with $current-entrypoint {
                %op-gen<exit>(0);
            }
            $label.position = $*MAST_FRAME.add-entrypoint($label.name);
            $current-entrypoint = $label.name;
            say "added entrypoint at position $label.position()";
        }
    }
}

for $parseresult.ast {
    if $_ ~~ Node {
        compile_node($_);
    }
}
$*MAST_FRAME.finish-labels();

.say for $*MAST_FRAME.strings.pairs;

say $*MAST_FRAME.bytecode.list.rotor(2).map(*.reverse.fmt("%02x", "").join() ~ " ");
say $*MAST_FRAME.bytecode.list.rotor(2).map({ :16(.reverse.fmt("%02x", "").join()).fmt("%4d") ~ " " });

dd $*MAST_FRAME.bytecode;

say $*MAST_FRAME.entrypoints;

say $*MAST_FRAME.entrypoints.perl;

my int @entrypoints = $*MAST_FRAME.entrypoints >>//>> 1;

sub run-the-program() {
    use nqp;
    nqp::installconfprog($*MAST_FRAME.bytecode, $*MAST_FRAME.strings, @entrypoints);
}

run-the-program();

use MoarVM::Profiler;

say profile {
    .say for $*MAST_FRAME.bytecode.list;
    say "profile ends now" for ^10;
};
