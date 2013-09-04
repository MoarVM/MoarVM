# This script processes the op list into a C header file that contains
# info about the opcodes.

class Op {
    has $.code;
    has $.name;
    has @.operands;
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
    $hf.say('MVMOpInfo * MVM_op_get_op(unsigned short op);');
    $hf.close;

    # Generate C file
    my $cf = open("src/core/ops.c", :w);
    $cf.say('#ifdef PARROT_OPS_BUILD');
    $cf.say('#define PARROT_IN_EXTENSION');
    $cf.say('#include "parrot/parrot.h"');
    $cf.say('#include "parrot/extend.h"');
    $cf.say('#include "sixmodelobject.h"');
    $cf.say('#include "nodes_parrot.h"');
    $cf.say('#include "../../src/core/ops.h"');
    $cf.say('#else');
    $cf.say('#include "moarvm.h"');
    $cf.say('#endif');
    $cf.say("/* This file is generated from $file by tools/update_ops.p6. */");
    $cf.say(opcode_details(@ops));
    $cf.say('MVMOpInfo * MVM_op_get_op(unsigned short op) {');
    $cf.say('    if (op >= MVM_op_counts)');
    $cf.say('        return NULL;');
    $cf.say('    return &MVM_op_infos[op];');
    $cf.say('}');
    $cf.close;

    # Generate NQP Ops file.
    my $nf = open("lib/MAST/Ops.nqp", :w);
    $nf.say("# This file is generated from $file by tools/update_ops.p6.");
    $nf.say("");
    $nf.say(op_constants(@ops));
    $nf.close;

    say "Wrote src/core/ops.h, src/core/ops.c, and lib/MAST/Ops.nqp";
}

# Parses ops and produces a bunch of Op objects.
sub parse_ops($file) {
    my @ops;
    my int $i = 0;
    for lines($file.IO) -> $line {
        if $line !~~ /^\s*[\#|$]/ {
            my ($name, @operands) = $line.split(/\s+/);
            @ops.push(Op.new(
                code     => $i,
                name     => $name,
                operands => @operands
            ));
            $i = $i + 1;
        }
    }
    return @ops;
}

# Generates MAST::Ops constants module.
sub op_constants(@ops) {
    join "\n", gather {
        take "class MAST::OpCode \{\n"~
        "    has \$!name;\n"~
        "    has \$!code;\n"~
        "    has \@!operands;\n"~
        "}\n"~
        "class MAST::Ops \{\n"~
        "    my \$MVM_operand_literal     := 0;\n"~
        "    my \$MVM_operand_read_reg    := 1;\n"~
        "    my \$MVM_operand_write_reg   := 2;\n"~
        "    my \$MVM_operand_read_lex    := 3;\n"~
        "    my \$MVM_operand_write_lex   := 4;\n"~
        "    my \$MVM_operand_rw_mask     := 7;\n"~
        "    my \$MVM_reg_int8            := 1;\n"~
        "    my \$MVM_reg_int16           := 2;\n"~
        "    my \$MVM_reg_int32           := 3;\n"~
        "    my \$MVM_reg_int64           := 4;\n"~
        "    my \$MVM_reg_num32           := 5;\n"~
        "    my \$MVM_reg_num64           := 6;\n"~
        "    my \$MVM_reg_str             := 7;\n"~
        "    my \$MVM_reg_obj             := 8;\n"~
        "    my \$MVM_operand_int8        := (\$MVM_reg_int8 * 8);\n"~
        "    my \$MVM_operand_int16       := (\$MVM_reg_int16 * 8);\n"~
        "    my \$MVM_operand_int32       := (\$MVM_reg_int32 * 8);\n"~
        "    my \$MVM_operand_int64       := (\$MVM_reg_int64 * 8);\n"~
        "    my \$MVM_operand_num32       := (\$MVM_reg_num32 * 8);\n"~
        "    my \$MVM_operand_num64       := (\$MVM_reg_num64 * 8);\n"~
        "    my \$MVM_operand_str         := (\$MVM_reg_str * 8);\n"~
        "    my \$MVM_operand_obj         := (\$MVM_reg_obj * 8);\n"~
        "    my \$MVM_operand_ins         := (9 * 8);\n"~
        "    my \$MVM_operand_type_var    := (10 * 8);\n"~
        "    my \$MVM_operand_lex_outer   := (11 * 8);\n"~
        "    my \$MVM_operand_coderef     := (12 * 8);\n"~
        "    my \$MVM_operand_callsite    := (13 * 8);\n"~
        "    my \$MVM_operand_type_mask   := (15 * 8);\n"~
        "    our \$ops_list := nqp::list();\n"~
        "    our \$operands_list := nqp::list();";
        for @ops -> $op {
            my $operands = $op.operands.map(&operand_flags).join(",\n        ");
            $operands = $operands.subst('MVM', '$MVM', :g);
            $operands = $operands.subst('|', '+|', :g);
            my $name = $op.name();
            my $code = $op.code();
            take
            "    our \$$name := $code;\n"~
            "    nqp::push(\$ops_list, MAST::OpCode.new(\n"~
            "        code => $code,\n"~
            "        name => '$name')\n"~
            "    );\n"~
            "    nqp::push(\$operands_list, nqp::list_i("~
            ($operands ?? "\n        $operands\n    "!!"")~
            "));";
        }
        take '}';
    }
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
        take "static MVMOpInfo MVM_op_infos[] = \{";
        for @ops -> $op {
            take "    \{";
            take "        MVM_OP_$op.name(),";
            take "        \"$op.name()\",";
            take "        $op.operands.elems(),";
            if $op.operands {
                take "        \{ $op.operands.map(&operand_flags).join(', ') }";
            }
            #else { take "        \{ }"; }
            take "    },"
        }
        take "};\n";
        take "static unsigned short MVM_op_counts = {+@ops};\n";
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
    token type     { < int8 int16 int32 int64 num32 num64 str obj > }
    token type_var { '`1' }
    token special  { < ins lo coderef callsite > }
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
        else {
            die "Failed to process operand '$operand'";
        }
    }
    else {
        die "Cannot parse operand '$operand'";
    }
}
