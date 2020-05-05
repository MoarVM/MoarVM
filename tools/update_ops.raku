#!/usr/bin/env perl6
# This script processes the op list into a C header file that contains
# info about the opcodes.

constant $EXT_BASE = 1024;
constant $EXT_CU_LIMIT = 1024;

# Figures out the various flags for an operand type.
grammar OperandFlag {
    token TOP {
        | <rw> '(' [ <type> | <type_var> ] ')'
        | <type>
        | <special>
    }
    token rw       { < rl wl r w > }
    token type     { < int8 int16 int32 int64 num32 num64 str obj uint8 uint16 uint32 uint64 > }
    token type_var { '`1' }
    token special  { < ins lo coderef callsite sslot > }
}

class Op {
    has $.code;
    has $.name;
    has $.mark;
    has @.operands;
    has %.adverbs;
    method generator() {
        my $offset = 2;
        if $.name eq "prepargs" {
            my $signature = @!operands.map({self.generate_arg($_, $++)}).join(', ');
            'sub (' ~ $signature ~ ') {' ~
            ~ 'die("MAST::Ops generator for prepargs NYI (QASTOperationsMAST is supposed to do it by itself)");'
            ~ '}';
        }
        elsif $.name eq "const_i64" {
            q:to/CODE/.subst("CODE_GOES_HERE", $.code).chomp;
            sub ($op0, int $value) {
                    my $bytecode := $*MAST_FRAME.bytecode;
                    my uint $elems := nqp::elems($bytecode);
                    my uint $index := nqp::unbox_u($op0);
                    if -32767 < $value && $value < 32768 {
                        nqp::writeuint($bytecode, $elems, 597, 5);
                        nqp::writeuint($bytecode, nqp::add_i($elems, 2), $index, 5);
                        nqp::writeuint($bytecode, nqp::add_i($elems, 4), $value, 5);
                    }
                    elsif -2147483647 < $value && $value < 2147483647 {
                        nqp::writeuint($bytecode, $elems, 598, 5);
                        nqp::writeuint($bytecode, nqp::add_i($elems, 2), $index, 5);
                        nqp::writeuint($bytecode, nqp::add_i($elems, 4), $value, 9);
                    }
                    else {
                        nqp::writeuint($bytecode, $elems, CODE_GOES_HERE, 5);
                        nqp::writeuint($bytecode, nqp::add_i($elems, 2), $index, 5);
                        nqp::writeuint($bytecode, nqp::add_i($elems, 4), $value, 13);
                    }
                }
            CODE
        } else {
            my $is-dispatch = $!mark eq '.d';
            my @params = @!operands.map({self.generate_arg($_, $++)});
            @params.push('@arg-indices') if $is-dispatch;
            my $signature = @params.join(', ');
            my $frame-getter = self.needs_frame
                ?? 'my $frame := $*MAST_FRAME; my $bytecode := $frame.bytecode;'
                !! 'my $bytecode := $*MAST_FRAME.bytecode;';
            my constant $prefix = "                ";
            my @operand-emitters = @!operands.map({ $prefix ~ self.generate_operand($_, $++, $offset)});
            if $is-dispatch {
                @operand-emitters.push: $prefix ~ 'my int $arg-offset := $elems + ' ~ $offset ~ ';';
                @operand-emitters.push: $prefix ~ 'for @arg-indices -> $offset { ' ~
                    'nqp::writeuint($bytecode, $arg-offset, nqp::unbox_u($offset), 5); $arg-offset := $arg-offset + 2; }'
            }
            my $operands-code = @operand-emitters.join("\n");
            'sub (' ~ $signature ~ ') {' ~ "\n" ~
                $frame-getter.indent(8) ~ ('
                my uint $elems := nqp::elems($bytecode);
                nqp::writeuint($bytecode, $elems, ' ~ $.code ~ ', 5);
' ~ $operands-code ~ '
            }').indent(-8);
        }
    }
    method needs_frame() {
        for @!operands -> $operand {
            if OperandFlag.parse($operand) -> (:$rw, :$type, :$type_var, :$special) {
                return True if !$rw and (($type // '') eq 'str' or ($special // '') eq 'ins' | 'coderef');
            }
        }
        return False;
    }
    method generate_arg($operand, $i) {
        if OperandFlag.parse($operand) -> (:$rw, :$type, :$type_var, :$special) {
            if !$rw {
                if $type {
                    $type ~ ' $op' ~ $i
                }
                elsif ($special // '') eq 'ins' {
                    '$op' ~ $i
                }
                elsif ($special // '') eq 'coderef' {
                    '$op' ~ $i
                }
                elsif ($special // '') eq 'callsite' {
                    '$op' ~ $i
                }
                elsif ($special // '') eq 'sslot' {
                    '$op' ~ $i
                }
                else {
                    '$op' ~ $i
                }
            }
            elsif $rw eq 'r' || $rw eq 'w' {
                '$op' ~ $i
            }
            elsif $rw eq 'rl' || $rw eq 'wl' {
                '$op' ~ $i
            }
        }
    }
    sub makewriteuint($offset is rw, $size, $var) {
        my $pos = $offset;
        $offset += $size;
        my $flags = $size * 2;
        $flags = 12 if $size == 8;
        $flags++; # force little endian
        'nqp::writeuint($bytecode, nqp::add_i($elems, ' ~ $pos ~ '), ' ~ $var ~ ', ' ~ $flags ~ ');'
    }
    sub makewritenum($offset is rw, $var) {
        my $pos = $offset;
        $offset += 8;
        'nqp::writenum($bytecode, nqp::add_i($elems, ' ~ $pos ~ '), ' ~ $var ~ ', 13)'
    }
    method generate_operand($operand, $i, $offset is rw) {
        if OperandFlag.parse($operand) -> (:$rw, :$type, :$type_var, :$special) {
            if !$rw {
                if ($type // '') eq 'int64' {
                    makewriteuint($offset, 8, '$op' ~ $i)
                }
                elsif ($type // '') eq 'int32' {
                    makewriteuint($offset, 8, '$op' ~ $i)
                }
                elsif ($type // '') eq 'uint32' {
                    makewriteuint($offset, 8, '$op' ~ $i)
                }
                elsif ($type // '') eq 'int16' {
                    makewriteuint($offset, 2, '$op' ~ $i)
                }
                elsif ($type // '') eq 'int8' {
                    makewriteuint($offset, 2, '$op' ~ $i)
                }
                elsif ($type // '') eq 'num64' {
                    makewritenum($offset, '$op' ~ $i)
                }
                elsif ($type // '') eq 'num32' {
                    makewritenum($offset, '$op' ~ $i)
                }
                elsif ($type // '') eq 'str' {
                    'my uint $index' ~ $i ~ ' := $frame.add-string($op' ~ $i ~ '); '
                    ~ makewriteuint($offset, 4, '$index' ~ $i);
                }
                elsif ($special // '') eq 'ins' {
                    $offset += 4;
                    "\$frame.compile_label(\$bytecode, \$op$i);"
                }
                elsif ($special // '') eq 'coderef' {
                    'my uint $index' ~ $i ~ ' := $frame.writer.get_frame_index($op' ~ $i ~ '); '
                    ~ makewriteuint($offset, 2, '$index' ~ $i)
                }
                elsif ($special // '') eq 'callsite' {
                    'my uint $index' ~ $i ~ ' := nqp::unbox_u($op' ~ $i ~ '); '
                    ~ makewriteuint($offset, 2, '$index' ~ $i)
                }
                elsif ($special // '') eq 'sslot' {
                }
                else {
                    die "literal operand type $type/$special NYI";
                }
            }
            elsif $rw eq 'r' || $rw eq 'w' {
#                nqp::die("Expected MAST::Local, but didn't get one. Got a " ~ $arg.HOW.name($arg) ~ " instead")
#                    unless nqp::istype($arg,MAST::Local);
#
#                my @local_types := self.local_types;
#                if $arg > nqp::elems(@local_types) {
#                    nqp::die("MAST::Local index out of range");
#                }
#                my $local_type := @local_types[$index];
#                if ($type != nqp::bitshiftl_i(type_to_local_type($local_type), 3) && $type != nqp::const::MVM_OPERAND_TYPE_VAR) {
#                    nqp::die("MAST::Local of wrong type specified: $type, expected $local_type (" ~ nqp::bitshiftl_i(type_to_local_type($local_type), 3) ~ ")");
#                }
                'my uint $index' ~ $i ~ ' := nqp::unbox_u($op' ~ $i ~ '); '

                ~ makewriteuint($offset, 2, '$index' ~ $i)
            }
            elsif $rw eq 'rl' || $rw eq 'wl' {
                q[nqp::die("Expected MAST::Lexical, but didn't get one") unless nqp::istype($op] ~ $i ~ q[, MAST::Lexical);]
                ~ 'my uint $index' ~ $i ~ ' := $op' ~ $i ~ '.index; '
                ~ 'my uint $frames_out' ~ $i ~ ' := $op' ~ $i ~ '.frames_out; '
                ~ makewriteuint($offset, 2, '$index' ~ $i)
                ~ makewriteuint($offset, 2, '$frames_out' ~ $i)
            }
            else {
                die("Unknown operand mode $rw cannot be compiled");
            }
        }
        else {
            die "Cannot parse operand '$operand'";
        }
    }
}

sub MAIN($file = "src/core/oplist") {
    # Parse the ops file to get the various ops.
    my @ops = parse_ops($file);
    say "Parsed {+@ops} total ops from src/core/oplist";

    # Generate header file.
    my $hf = open("src/core/ops.h", :w);
    $hf.say("/* This file is generated from $file by tools/update_ops.raku. */");
    $hf.say("");
    $hf.say(opcode_defines(@ops));
    $hf.say("#define MVM_OP_EXT_BASE $EXT_BASE");
    $hf.say("#define MVM_OP_EXT_CU_LIMIT $EXT_CU_LIMIT");
    $hf.say('');
    $hf.say('MVM_PUBLIC const MVMOpInfo * MVM_op_get_op(unsigned short op);');
    $hf.say('MVM_PUBLIC MVMuint8 MVM_op_is_allowed_in_confprog(unsigned short op);');
    $hf.say('MVM_PUBLIC const char * MVM_op_get_mark(unsigned short op);');
    $hf.close;

    # Generate C file
    my $cf = open("src/core/ops.c", :w);
    $cf.say(qq:to/CORE_OPS/);
        #include "moar.h"
        /* This file is generated from $file by tools/update_ops.raku. */
        { opcode_details(@ops) }
        MVM_PUBLIC const MVMOpInfo * MVM_op_get_op(unsigned short op) \{
            if (op >= MVM_op_counts)
                return NULL;
            return \&MVM_op_infos[op];
        }

        MVM_PUBLIC MVMuint8 MVM_op_is_allowed_in_confprog(unsigned short op) \{
            if (op > last_op_allowed)
                return 0;
            return !!(MVM_op_allowed_in_confprog[op / 8] & (1 << (op % 8)));
        }

        MVM_PUBLIC const char *MVM_op_get_mark(unsigned short op) \{
{ mark_spans(@ops) }
        }

        CORE_OPS
    $cf.close;

    # Generate cgoto labels header.
    my $lf = open('src/core/oplabels.h', :w);
    $lf.say("/* This file is generated from $file by tools/update_ops.raku. */");
    $lf.say("");
    $lf.say(op_labels(@ops));
    $lf.close;

    my %op_constants = op_constants(@ops);

    # Generate NQP Ops file.
    my $nf = open("lib/MAST/Ops.nqp", :w);
    $nf.say("# This file is generated from $file by tools/update_ops.raku.");
    $nf.say("");
    $nf.say(%op_constants<NQP>);
    $nf.close;

    # Generate a Raku Ops file into the tools directory
    my $pf = open("tools/lib/MAST/Ops.pm", :w);
    $pf.say("# This file is generated from $file by tools/update_ops.raku.");
    $pf.say("");
    $pf.say(%op_constants<Raku>);
    $pf.close;

    say "Wrote src/core/ops.h, src/core/ops.c, src/core/oplabels.h, tools/lib/MAST/Ops.pm, and lib/MAST/Ops.nqp";
}

# Parses ops and produces a bunch of Op objects.
sub parse_ops($file) {
    my @ops;
    my int $i = 0;
    for lines($file.IO) -> $line {
        if $line !~~ /^\s*['#'|$]/ {
            my ($name, $mark, @operands) = $line.split(/\s+/);

            # Look for validation mark.
            unless $mark && $mark ~~ /^ <[:.+*-]> \w $/ {
                @operands.unshift($mark) if $mark;
                $mark = '  ';
            }

            # Look for operands that are actually adverbs.
            my %adverbs;
            while @operands && @operands[*-1] ~~ /^ ':' (\w+) $/ {
                %adverbs{$0} = 1;
                @operands.pop;
            }

            @ops.push(Op.new(
                code     => $i,
                name     => $name,
                mark     => $mark,
                operands => @operands,
                adverbs  => %adverbs
            ));
            $i = $i + 1;
        }
    }
    return @ops;
}

my $value_map = {
    'MVM_operand_literal' => 0,
    'MVM_operand_read_reg' => 1,
    'MVM_operand_write_reg' => 2,
    'MVM_operand_read_lex' => 3,
    'MVM_operand_write_lex' => 4,
    'MVM_operand_rw_mask' => 7,
    'MVM_reg_int8' => 1,
    'MVM_reg_int16' => 2,
    'MVM_reg_int32' => 3,
    'MVM_reg_int64' => 4,
    'MVM_reg_num32' => 5,
    'MVM_reg_num64' => 6,
    'MVM_reg_str' => 7,
    'MVM_reg_obj' => 8,
    'MVM_reg_uint8' => 17,
    'MVM_reg_uint16' => 18,
    'MVM_reg_uint32' => 19,
    'MVM_reg_uint64' => 20,
    'MVM_operand_int8' => 8,
    'MVM_operand_int16' => 16,
    'MVM_operand_int32' => 24,
    'MVM_operand_int64' => 32,
    'MVM_operand_num32' => 40,
    'MVM_operand_num64' => 48,
    'MVM_operand_str' => 56,
    'MVM_operand_obj' => 64,
    'MVM_operand_ins' => 72,
    'MVM_operand_type_var' => 80,
    'MVM_operand_lex_outer' => 88,
    'MVM_operand_coderef' => 96,
    'MVM_operand_callsite' => 104,
    'MVM_operand_type_mask' => 248,
    'MVM_operand_spesh_slot' => 128,
    'MVM_operand_uint8' => 136,
    'MVM_operand_uint16' => 144,
    'MVM_operand_uint32' => 152,
    'MVM_operand_uint64' => 160
};

# Generates MAST::Ops constants module.
sub op_constants(@ops is copy) {
    my @offsets;
    my @counts;
    my @values;
    my $values_idx = 0;
    @ops .= grep: {$_.mark ne '.s' and not $_.name.starts-with(any('sp_','prof_','DEPRECTAED'))};
    for @ops -> $op {
        my $last_idx = $values_idx;
        @offsets.push($values_idx);
        for $op.operands.map(&operand_flags_values) -> $operand {
            @values.push($operand);
            $values_idx++;
        }
        @counts.push($values_idx - $last_idx);
    }
    return (
        NQP => '
class MAST::Ops {}
BEGIN {
    MAST::Ops.WHO<@offsets> := nqp::list_i('~
        join(",\n    ", @offsets)~');
    MAST::Ops.WHO<@counts> := nqp::list_i('~
        join(",\n    ", @counts)~');
    MAST::Ops.WHO<@values> := nqp::list_i('~
        join(",\n    ", @values)~');
    MAST::Ops.WHO<%codes> := nqp::hash('~
        join(",\n    ", @ops.map({ "'$_.name()', $_.code()" }))~');
    MAST::Ops.WHO<@names> := nqp::list_s('~
        join(",\n    ", @ops.map({ "'$_.name()'" }))~');
    MAST::Ops.WHO<%generators> := nqp::hash('~
        join(",\n    ", @ops.map({ "'$_.name()', $_.generator()" }))~');
}',
        Raku => '
unit module MAST::Ops;
our %flags is export = ('~
    join(",\n    ", $value_map.pairs.sort(*.value).map({ $_.perl }) )~');
our @offsets is export = '~
    join(",\n    ", @offsets)~';
our @counts = '~
    join(",\n    ", @counts)~';
our @values is export = '~
    join(",\n    ", @values)~';
our %codes is export = '~
    join(",\n    ", @ops.map({ "'$_.name()', $_.code()" }))~';
our @names is export = '~
    join(",\n    ", @ops.map({ "'$_.name()'" }))~';
',
        ).hash;
}

# Generate labels for cgoto dispatch
sub op_labels(@ops) {
    my @labels = @ops.map({ sprintf('&&OP_%s', $_.name) });
    my @padding = 'NULL' xx $EXT_BASE - @ops;
    my @extlabels = '&&OP_CALL_EXTOP' xx $EXT_CU_LIMIT;
    return "static const void * const LABELS[] = \{\n    {
        join(",\n    ", @labels, @padding, @extlabels)
    }\n\};";
}

# Creates the #defines for the ops.
sub opcode_defines(@ops) {
    join "\n", gather {
        take "/* Op name defines. */";
        for @ops -> $op {
            take "#define MVM_OP_$op.name() $op.code()";
        }
        take "";
    }
}

# Creates the static array of opcode info.
sub opcode_details(@ops) {
    (join "\n", gather {
        take "static const MVMOpInfo MVM_op_infos[] = \{";
        for @ops -> $op {
            take "    \{";
            take "        MVM_OP_$op.name(),";
            take "        \"$op.name()\",";
            take "        $op.operands.elems(),";
            take "        $($op.adverbs<pure> ?? '1' !! '0'),";
            take "        $(
                ($op.adverbs<deoptonepoint> ?? 1 !! 0) +
                ($op.adverbs<deoptallpoint> ?? 2 !! 0) +
                ($op.adverbs<osrpoint> ?? 4 !! 0) +
                ($op.adverbs<predeoptonepoint> ?? 8 !! 0)),";
            take "        $($op.adverbs<maycausedeopt> ?? '1' !! '0'),";
            take "        $($op.adverbs<logged> ?? '1' !! '0'),";
            take "        $($op.adverbs<noinline> ?? '1' !! '0'),";
            take "        $(($op.adverbs<invokish> ?? 1 !! 0) +
                            ($op.adverbs<throwish> ?? 2 !! 0)),";
            take "        $($op.adverbs<useshll> ?? '1' !! '0'),";
            take "        $($op.adverbs<specializable> ?? '1' !! '0'),";
            take "        $($op.adverbs<cache> ?? '1' !! '0'),";
            if $op.operands {
                take "        \{ $op.operands.map(&operand_flags).join(', ') }";
            }
            else { take "        \{ 0 }"; }
            take "    },"
        }
        take "};\n";
        take "static const unsigned short MVM_op_counts = {+@ops};\n";
    })
    ~ "\n" ~
    (join "", gather {
        my $end-of-normal-ops = @ops.grep(*.mark eq ".s", :k).head;
        take "static const MVMuint16 last_op_allowed = $($end-of-normal-ops - 1);\n\n";
        take "static const MVMuint8 MVM_op_allowed_in_confprog[] = \{";
        for @ops.head($end-of-normal-ops - 1).rotor(8, :partial) -> @eightops {
            my @bits = @eightops.map(*.adverbs<confprog>.so.Int);
            @bits.push: 0 while @bits < 8;
            my $integer = :2[@bits.reverse];
            if $++ %% 4 {
                take "\n    ";
            }
            else {
                take " ";
            }
            take "0x$integer.base(16),";
        }
        take "};\n";
    })
}

# Create code to look up an op's mark
# since marks are rare in the first section of the op list
# and all marks after a certain point have one (spesh ops),
# and the marks are only used by the validator anyway,
# we can leave them out of the MVM_op_infos array that has
# data used all over the place, thus saving a little bit of
# memory.
# 
# We foolishly(?) rely on the first op to not have a mark
sub mark_spans(@ops) {
    my %current;
    my @spans;
    %current<mark> = "  ";
    die "expected first op to not have a mark" unless @ops.head.mark eq "  ";
    sub store-span {
        @spans.push(Map.new(%current>>.clone));
    }
    sub make-lookup-code {
        die "expected last span of ops to have the .s mark" unless @spans.tail.<mark> eq ".s";
        my $spesh-start = @spans.pop.<start>;
        my @pieces;
        @pieces.push: q:s:to/CODE/;
            if (op > $spesh-start && op < MVM_OP_EXT_BASE) {
                return ".s";
            CODE
        for @spans {
            if .<count> == 1 {
                @pieces.push: qq:to/CODE/;
                    } else if (op == $_.<start>) \{
                        return "{ .<mark> }";
                    CODE
            }
            else {
                @pieces.push: qq:to/CODE/;
                    } else if (op >= $_.<start> && op < { .<start> + .<count> }) \{
                        return "{ .<mark> }";
                    CODE
            }
        }
        @pieces.push: qq:to/CODE/;
            } else if (op >= MVM_OP_EXT_BASE) \{
                return ".x";
            CODE
        @pieces.push: "}\n";
        @pieces.push: 'return "  ";';
        @pieces.join().indent(4);
    }
    for @ops {
        my $mark = .mark();
        if $mark ne '  ' {
            if %current<mark> eq $mark {
                %current<count>++;
            }
            else {
                if %current<mark> ne "  " {
                    store-span;
                }
                %current<mark> = $mark;
                %current<count> = 1;
                %current<start> = .code;
            }
        }
        else {
            if %current<mark> ne "  " {
                store-span;
            }
            %current<mark> = "  ";
        }
    }
    store-span;

    make-lookup-code;
}

my %rwflags = (
    r  => 'MVM_operand_read_reg',
    w  => 'MVM_operand_write_reg',
    rl => 'MVM_operand_read_lex',
    wl => 'MVM_operand_write_lex'
);
sub operand_flags($operand) {
    if OperandFlag.parse($operand) -> (:$rw, :$type, :$type_var, :$special) {
        if $rw {
            %rwflags{$rw} ~ ' | ' ~ ($type ?? "MVM_operand_$type" !! 'MVM_operand_type_var')
        }
        elsif $type {
            "MVM_operand_$type"
        }
        elsif $special eq 'ins' {
            'MVM_operand_ins'
        }
        elsif $special eq 'lo' {
            'MVM_operand_lex_outer'
        }
        elsif $special eq 'coderef' {
            'MVM_operand_coderef'
        }
        elsif $special eq 'callsite' {
            'MVM_operand_callsite'
        }
        elsif $special eq 'sslot' {
            'MVM_operand_spesh_slot'
        }
        else {
            die "Failed to process operand '$operand'";
        }
    }
    else {
        die "Cannot parse operand '$operand'";
    }
}

sub operand_flags_values($operand) {
    if OperandFlag.parse($operand) -> (:$rw, :$type, :$type_var, :$special) {
        if $rw {
            $value_map{%rwflags{$rw}} +| $value_map{($type ?? "MVM_operand_$type" !! 'MVM_operand_type_var')}
        }
        elsif $type {
            $value_map{"MVM_operand_$type"}
        }
        elsif $special eq 'ins' {
            $value_map{'MVM_operand_ins'}
        }
        elsif $special eq 'lo' {
            $value_map{'MVM_operand_lex_outer'}
        }
        elsif $special eq 'coderef' {
            $value_map{'MVM_operand_coderef'}
        }
        elsif $special eq 'callsite' {
            $value_map{'MVM_operand_callsite'}
        }
        elsif $special eq 'sslot' {
            $value_map{'MVM_operand_spesh_slot'}
        }
        else {
            die "Failed to process operand '$operand'";
        }
    }
    else {
        die "Cannot parse operand '$operand'";
    }
}
