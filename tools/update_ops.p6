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

sub redirect-to($path, &block) {
    my $*OUT = open($path, :w);
    block;
    $*OUT.close;
}

sub MAIN($file = "src/core/oplist") {
    # Parse the ops file to get the various ops.
    my @ops = parse_ops($file);
    say "Parsed {+@ops} total ops from src/core/oplist";

    # Generate header file.
    redirect-to "src/core/ops.h", {
        say "/* This file is generated from $file by tools/update_ops.p6. */";
        say "";
        say opcode_defines(@ops);
        say "#define MVM_OP_EXT_BASE $EXT_BASE";
        say "#define MVM_OP_EXT_CU_LIMIT $EXT_CU_LIMIT";
        say '';
        say 'MVM_PUBLIC const MVMOpInfo * MVM_op_get_op(unsigned short op);';
    }

    # Generate C file
    redirect-to "src/core/ops.c", {
        say '#include "moar.h"';
        say "/* This file is generated from $file by tools/update_ops.p6. */";
        say opcode_details(@ops);
        say 'MVM_PUBLIC const MVMOpInfo * MVM_op_get_op(unsigned short op) {';
        say '    if (op >= MVM_op_counts)';
        say '        return NULL;';
        say '    return &MVM_op_infos[op];';
        say '}';
    }

    # Generate cgoto labels header.
    redirect-to "src/core/oplabels.h", {
        say "/* This file is generated from $file by tools/update_ops.p6. */";
        say "";
        say op_labels(@ops);
    }

    # Generate NQP Ops file.
    redirect-to "lib/MAST/Ops.nqp", {
        say "# This file is generated from $file by tools/update_ops.p6.";
        say "";
        say op_constants(@ops);
    }

    say "Wrote src/core/ops.h, src/core/ops.c, src/core/oplabels.h and lib/MAST/Ops.nqp";
}

# Parses ops and produces a bunch of Op objects.
sub parse_ops($file) {
    gather for lines($file.IO) -> $line {
        my grammar Op::Line {
            regex TOP { <comment-line> | <op-line> }
            regex comment-line { \h* ['#'\N*]? }
            regex op-line { <name> \h* <mark>? \h* <operand>* % [\h+] \h* <adverb>* % [\h+] $$
                {
                    my $code = (state $)++;
                    my $name = ~$<name>;
                    my $mark = ~($<mark> // '  ');
                    my @operands = @<operand>».ast;
                    my %adverbs = @<adverb>.map(-> $/ { $0 => $1 || 1 });

                    take Op.new(:$code, :$name, :$mark, :@operands, :%adverbs);
                }
            }
            token name { \H+ }
            token mark { <[:.+*-]> \w >> }
            token operand { <!before ':'>
                [
                | <rw> '(' [ <type> | <type_var> ] ')'
                | <type>
                | <special>
                ]
                {
                    make {
                        :rw(~($<rw> // '')),
                        :type(~($<type> // '')),
                        :type_var(~($<type_var> // '')),
                        :special(~($<special> // '')),
                    }
                }
            }
            token rw       { < rl wl r w > }
            token type     { < int8 int16 int32 int64 num32 num64 str obj > }
            token type_var { '`1' }
            token special  { < ins lo coderef callsite sslot > }
            token adverb { ':' (\w+) [ '(' (<-[)]>+) ')' ]? }
        };

        Op::Line.parse($line)
            or die "Couldn't parse line '$line'";

    }
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
    'MVM_operand_spesh_slot' => 128
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
    return '
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
    MAST::Ops.WHO<@names> := nqp::list('~
        join(",\n    ", @ops.map({ "'$_.name()'" }))~');
}';
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
                ($op.adverbs<osrpoint> ?? 4 !! 0)),";
            take "        $($op.adverbs<noinline> ?? '1' !! '0'),";
            take "        $(($op.adverbs<invokish> ?? 1 !! 0) +
                            ($op.adverbs<throwish> ?? 2 !! 0)),";
            if $op.operands {
                take "        \{ $op.operands.map(&operand_flags).join(', ') },";
            }
            else { take "        \{ 0 },"; }
            if $op.adverbs<esc> -> $esc_info {
                take escape_info($esc_info);
            }
            else { take "        \{ 0 }"; }
            take "    },"
        }
        take "};\n";
        take "static const unsigned short MVM_op_counts = {+@ops};\n";
    }
}

sub escape_info($info) {
    sub flag($_) {
        when '-' { 'MVM_ESCAPE_IRR' }
        when 'y' { 'MVM_ESCAPE_YES' }
        when 'n' { 'MVM_ESCAPE_NO' }
        when /into '[' (\d+) ']'/ {
            'MVM_ESCAPE_INTO + ' ~ ($0 +< 3)
        }
        when /outof '[' (\d+) ']'/ {
            'MVM_ESCAPE_OUTOF + ' ~ ($0 +< 3)
        }
        default { die "Unknown escape flag $_" }
    }
    return "        \{ $( $_ ?? .map(&flag).join(', ') !! '0' ) }"
        given $info.split(',');
}

# Figures out the various flags for an operand type.
my %rwflags = (
    r  => 'MVM_operand_read_reg',
    w  => 'MVM_operand_write_reg',
    rl => 'MVM_operand_read_lex',
    wl => 'MVM_operand_write_lex'
);
sub operand_flags($operand) {
    given $operand -> (:$rw, :$type, :$type_var, :$special) {
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
}

sub operand_flags_values($operand) {
    given $operand -> (:$rw, :$type, :$type_var, :$special) {
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
}
