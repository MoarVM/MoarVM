#!/usr/bin/env perl6
# This script processes the op list into a C header file that contains
# info about the opcodes.

constant $EXT_BASE = 1024;
constant $EXT_CU_LIMIT = 1024;

class Op {
    has $.code;
    has $.name;
    has $.mark;
    has @.operands;
    has %.adverbs;
}

sub MAIN($file = "src/core/oplist") {
    # Parse the ops file to get the various ops.
    my @ops = parse_ops($file);
    say "Parsed {+@ops} total ops from src/core/oplist";

    # Generate header file.
    my $hf = open("src/core/ops.h", :w);
    $hf.say("/* This file is generated from $file by tools/update_ops.p6. */");
    $hf.say("");
    $hf.say(opcode_defines(@ops));
    $hf.say("#define MVM_OP_EXT_BASE $EXT_BASE");
    $hf.say("#define MVM_OP_EXT_CU_LIMIT $EXT_CU_LIMIT");
    $hf.say('');
    $hf.say('MVM_PUBLIC const MVMOpInfo * MVM_op_get_op(unsigned short op);');
    $hf.close;

    # Generate C file
    my $cf = open("src/core/ops.c", :w);
    $cf.say('#include "moar.h"');
    $cf.say("/* This file is generated from $file by tools/update_ops.p6. */");
    $cf.say(opcode_details(@ops));
    $cf.say('MVM_PUBLIC const MVMOpInfo * MVM_op_get_op(unsigned short op) {');
    $cf.say('    if (op >= MVM_op_counts)');
    $cf.say('        return NULL;');
    $cf.say('    return &MVM_op_infos[op];');
    $cf.say('}');
    $cf.close;

    # Generate cgoto labels header.
    my $lf = open('src/core/oplabels.h', :w);
    $lf.say("/* This file is generated from $file by tools/update_ops.p6. */");
    $lf.say("");
    $lf.say(op_labels(@ops));
    $lf.close;

    my %op_constants = op_constants(@ops);

    # Generate NQP Ops file.
    my $nf = open("lib/MAST/Ops.nqp", :w);
    $nf.say("# This file is generated from $file by tools/update_ops.p6.");
    $nf.say("");
    $nf.say(%op_constants<NQP>);
    $nf.close;

    # Generate a p6 Ops file into the tools directory
    my $pf = open("tools/lib/MAST/Ops.pm", :w);
    $pf.say("# This file is generated from $file by tools/update_ops.p6.");
    $pf.say("");
    $pf.say(%op_constants<P6>);
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
    'MVM_operand_type_mask' => 120,
    'MVM_operand_spesh_slot' => 128,
    'MVM_operand_uint8' => 136,
    'MVM_operand_uint16' => 144,
    'MVM_operand_uint32' => 152,
    'MVM_operand_uint64' => 160
};

# Generates MAST::Ops constants module.
sub op_constants(@ops) {
    my @offsets;
    my @counts;
    my @values;
    my $values_idx = 0;
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
}',
        P6 => '
unit module MAST::Ops;
our %flags is export = ('~
    join(",\n    ", $value_map.pairs.map({ $_.perl }) )~');
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
    join "\n", gather {
        take "static const MVMOpInfo MVM_op_infos[] = \{";
        for @ops -> $op {
            take "    \{";
            take "        MVM_OP_$op.name(),";
            take "        \"$op.name()\",";
            take "        \"$op.mark()\",";
            take "        $op.operands.elems(),";
            take "        $($op.adverbs<pure> ?? '1' !! '0'),";
            take "        $(
                ($op.adverbs<deoptonepoint> ?? 1 !! 0) +
                ($op.adverbs<deoptallpoint> ?? 2 !! 0) +
                ($op.adverbs<osrpoint> ?? 4 !! 0) +
                ($op.adverbs<predeoptonepoint> ?? 8 !! 0)),";
            take "        $($op.adverbs<logged> ?? '1' !! '0'),";
            take "        $($op.adverbs<noinline> ?? '1' !! '0'),";
            take "        $(($op.adverbs<invokish> ?? 1 !! 0) +
                            ($op.adverbs<throwish> ?? 2 !! 0)),";
            if $op.operands {
                take "        \{ $op.operands.map(&operand_flags).join(', ') }";
            }
            #else { take "        \{ }"; }
            take "    },"
        }
        take "};\n";
        take "static const unsigned short MVM_op_counts = {+@ops};\n";
    }
}

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
