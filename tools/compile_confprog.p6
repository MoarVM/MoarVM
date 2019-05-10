use Grammar::Tracer;

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
    log = ~(+(~(5)));
    log = "hi";
    log = ~(+("99"));
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
        file_location => CString,
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
        <[1..9]> '_'? <[0..9]>* % '_'?
    }
    regex one_expression:<literal_number_base16> {
        "0x" <[1..9a..fA..F]> '_'? <[0..9a..fA..F]>* % '_'?
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
         <one_expression> [\s* <compop> \s* <other_expression=.one_expression>]?
    }

    regex one_expression:<parenthesized> {
        '(' \s* <expression> \s* ')'
    }
    regex one_expression:<prefixed> {
        <prefixop> '(' \s* <expression> \s* ')'
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
>;

my %prefix-to-op = <
    ~ stringify
    + intify
    ! negate
>;

class ConfProgActions {
    method prefixop($/) { make $/.Str }
    method compop($/)   { say $/.Str; make %op-to-op{$/.Str} }

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
        if $<other_expression> && $<compop> {
            make Op.new( op => $<compop>.ast,
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

    method statement:<entrypoint>($/) { make { entrypoint => $<entrypoint>.Str } }
    method statement:<label>($/) { make { label => $<ident>.Str } }

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
);

use MASTOps:from<NQP>;

my %op-gen := MAST::Ops.WHO<%generators>;

my $*MAST_FRAME = class { has uint8 @.bytecode; method add-string($str) { say "$str added" }; method compile_label($bytecode, $label) { say "compiling label $label" } }.new;

#%op-gen<say>(1);

say $*MAST_FRAME.bytecode;

my $parseresult = ConfProg.parse($testprog, actions => ConfProgActions.new);

"$_[1].perl()".say && $_[0].&ddt && "".say for $parseresult.ast Z $parseresult<statement>.list>>.Str;

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
        when any(<and_i or_i>) {
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
                    ddt $rhs;
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
ddt $parseresult.ast;
say "";
say "lexpad";
say "";
ddt %*LEXPAD;

enum SpecialRegister <
    STRUCT_SELECT
    STRUCT_ACCUMULATOR
    FEATURE_TOGGLE
>;

enum RegisterType <
    RegMVMObject
    RegStruct
    RegInteger
    RegString
    RegCString
>;

my %CPTypeToRegType := :{
    CPInt => RegInteger,
    CPString => RegString,
};

sub to-reg-type($type) {
    with %CPTypeToRegType{$type} {
        return $_
    }
    return RegStruct;
}

constant $custom_reg_base = +SpecialRegister::.keys;

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

multi sub compile_node(SVal $val, :$target!) {
    note "const_s into $target";
    %op-gen<const_s>($target, $val.value);
}
multi sub compile_node(IVal $val, :$target!) {
    note "const_i into $target";
    %op-gen<const_i64>($target, $val.value);
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
            my $type = $op.children[1];

            my $targetreg;
            my $targetregtype = to-reg-type($value.type);

            if $targetregtype eqv RegStruct {
                $targetreg = STRUCT_ACCUMULATOR
            }
            else {
                $targetreg = $*REGALLOC.fresh($targetregtype)
            }

            # select right struct type
            # this ends up as two noops after the validator has
            # seen it, but the getattr that comes next will use
            # the type identified here for the struct type.
            %op-gen<const_s>(STRUCT_SELECT, $type);

            #%op-gen<getattr_o>(

            if $targetreg != STRUCT_ACCUMULATOR {
                $*REGALLOC.free($targetreg)
            }
        }
        default {
            die "cannot compile $op.op() yet";
        }
    }
}

for $parseresult.ast {
    if $_ ~~ Node {
        compile_node($_);
    }
}

say $*MAST_FRAME.bytecode.list.rotor(2).map(*.reverse.fmt("%02x", "").join() ~ " ");
say $*MAST_FRAME.bytecode.list.rotor(2).map({ :16(.reverse.fmt("%02x", "").join()).fmt("%4d") ~ " " });

