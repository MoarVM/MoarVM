# This script processes the op list into a C header file that contains
# info about the opcodes.

class Op {
    has $.code;
    has $.name;
    has @.operands;
}
class Bank {
    has $.name;
    has @.ops;
}

sub MAIN($file = "src/core/oplist") {
    # Parse the ops file and get the various op banks.
    my @banks = parse_ops($file);
    say "Parsed {+@banks} op banks with {[+] @banks>>.ops>>.elems } total ops";

    # Generate header file.
    my $hf = open("src/core/ops.h", :w);
    $hf.say("/* This file is generated from $file by tools/update_ops.p6. */");
    $hf.say("");
    $hf.say(bank_defines(@banks));
    $hf.say(opcode_defines(@banks));
    $hf.say('MVMOpInfo * MVM_op_get_op(unsigned char bank, unsigned char op);');
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
    $cf.say(opcode_details(@banks));
    $cf.say('MVMOpInfo * MVM_op_get_op(unsigned char bank, unsigned char op) {');
    $cf.say('    if (bank >= MVM_op_banks || op >= MVM_opcounts_by_bank[bank])');
    $cf.say('        return NULL;');
    $cf.say('    return &MVM_op_info[bank][op];');
    $cf.say('}');
    $cf.close;

    # Generate NQP Ops file.
    my $nf = open("lib/MAST/Ops.nqp", :w);
    $nf.say("# This file is generated from $file by tools/update_ops.p6.");
    $nf.say("");
    $nf.say(bank_constants(@banks));
    $nf.say(op_constants(@banks));
    $nf.close;

    say "Wrote ops.h, ops.c, and Ops.nqp";
}

# Parses ops and produces a bunch of Op and Bank objects.
sub parse_ops($file) {
    my @banks;
    my $cur_bank;
    for lines($file.IO) -> $line {
        if $line ~~ /^BANK <.ws> (\d+) <.ws> (.+)$/ {
            $cur_bank = +$0;
            @banks[$cur_bank] //= Bank.new(name => ~$1);
        }
        elsif $line ~~ /^\#/ {
            # comments is ignored.
        }
        elsif $line !~~ /^\s*$/ {
            die "Op declaration before bank declaration" unless @banks[$cur_bank];
            my ($code, $name, @operands) = $line.split(/\s+/);
            @banks[$cur_bank].ops.push(Op.new(
                code     => :16($code.substr(2)),
                name     => $name,
                operands => @operands
            ));
        }
    }
    return @banks;
}

# Generates MAST::OpBanks constants module.
sub bank_constants(@banks) {
    join "\n", gather {
        take 'class MAST::OpBanks {';
        for @banks.kv -> $i, $b {
            take "    our \$$b.name() := $i;";
        }
        take '}';
    }
}

# Generates MAST::Ops constants module.
sub op_constants(@banks) {
    join "\n", gather {
        take 'class MAST::Operands {';
        take '    our $MVM_operand_literal     := 0;';
        take '    our $MVM_operand_read_reg    := 1;';
        take '    our $MVM_operand_write_reg   := 2;';
        take '    our $MVM_operand_read_lex    := 3;';
        take '    our $MVM_operand_write_lex   := 4;';
        take '    our $MVM_operand_rw_mask     := 7;';
        take '    our $MVM_reg_int8            := 1;';
        take '    our $MVM_reg_int16           := 2;';
        take '    our $MVM_reg_int32           := 3;';
        take '    our $MVM_reg_int64           := 4;';
        take '    our $MVM_reg_num32           := 5;';
        take '    our $MVM_reg_num64           := 6;';
        take '    our $MVM_reg_str             := 7;';
        take '    our $MVM_reg_obj             := 8;';
        take '    our $MVM_operand_int8        := ($MVM_reg_int8 * 8);';
        take '    our $MVM_operand_int16       := ($MVM_reg_int16 * 8);';
        take '    our $MVM_operand_int32       := ($MVM_reg_int32 * 8);';
        take '    our $MVM_operand_int64       := ($MVM_reg_int64 * 8);';
        take '    our $MVM_operand_num32       := ($MVM_reg_num32 * 8);';
        take '    our $MVM_operand_num64       := ($MVM_reg_num64 * 8);';
        take '    our $MVM_operand_str         := ($MVM_reg_str * 8);';
        take '    our $MVM_operand_obj         := ($MVM_reg_obj * 8);';
        take '    our $MVM_operand_ins         := (9 * 8);';
        take '    our $MVM_operand_type_var    := (10 * 8);';
        take '    our $MVM_operand_lex_outer   := (11 * 8);';
        take '    our $MVM_operand_coderef     := (12 * 8);';
        take '    our $MVM_operand_callsite    := (13 * 8);';
        take '    our $MVM_operand_type_mask   := (15 * 8);';
        take '}';
        take "\n";
        take 'class MAST::Ops {';
        take '    my $MVM_operand_literal     := 0;';
        take '    my $MVM_operand_read_reg    := 1;';
        take '    my $MVM_operand_write_reg   := 2;';
        take '    my $MVM_operand_read_lex    := 3;';
        take '    my $MVM_operand_write_lex   := 4;';
        take '    my $MVM_operand_rw_mask     := 7;';
        take '    my $MVM_reg_int8            := 1;';
        take '    my $MVM_reg_int16           := 2;';
        take '    my $MVM_reg_int32           := 3;';
        take '    my $MVM_reg_int64           := 4;';
        take '    my $MVM_reg_num32           := 5;';
        take '    my $MVM_reg_num64           := 6;';
        take '    my $MVM_reg_str             := 7;';
        take '    my $MVM_reg_obj             := 8;';
        take '    my $MVM_operand_int8        := ($MVM_reg_int8 * 8);';
        take '    my $MVM_operand_int16       := ($MVM_reg_int16 * 8);';
        take '    my $MVM_operand_int32       := ($MVM_reg_int32 * 8);';
        take '    my $MVM_operand_int64       := ($MVM_reg_int64 * 8);';
        take '    my $MVM_operand_num32       := ($MVM_reg_num32 * 8);';
        take '    my $MVM_operand_num64       := ($MVM_reg_num64 * 8);';
        take '    my $MVM_operand_str         := ($MVM_reg_str * 8);';
        take '    my $MVM_operand_obj         := ($MVM_reg_obj * 8);';
        take '    my $MVM_operand_ins         := (9 * 8);';
        take '    my $MVM_operand_type_var    := (10 * 8);';
        take '    my $MVM_operand_lex_outer   := (11 * 8);';
        take '    my $MVM_operand_coderef     := (12 * 8);';
        take '    my $MVM_operand_callsite    := (13 * 8);';
        take '    my $MVM_operand_type_mask   := (15 * 8);';
        take '    our $allops := [';
        take join(",\n", gather {
            for @banks -> $bank {
                take "        [\n" ~ join(",\n", gather {
                    for $bank.ops -> $op {
                        my $operands = $op.operands.map(&operand_flags).join(",\n                    ");
                        $operands = $operands.subst('MVM', '$MVM', :g);
                        $operands = $operands.subst('|', '+|', :g);
                        take join "\n", gather {
                            take "            '$op.name()', nqp::hash(";
                            take "                'code', $op.code(),";
                            take "                'operands', [";
                            take "                    $operands" if $operands;
                            take "                ]";
                            take "            )";
                        };
                    }
                }) ~ "\n        ]";
            }
        });
        take '    ];';
        for @banks.kv -> $i, $bank {
            take "    our \$$bank.name() := nqp::hash();";
            take '    for $allops['~$i~'] -> $opname, $opdetails {';
            take '        $'~$bank.name()~'{$opname} := $opdetails;';
            take '    }';
        }
        take '}';
    }
}

# Creates the #defines for the banks.
sub bank_defines(@banks) {
    join "\n", gather {
        take "/* Bank name defines. */";
        for @banks.kv -> $i, $b {
            take "#define MVM_OP_BANK_$b.name() $i";
        }
        take "";
    }
}

# Creates the #defines for the ops.
sub opcode_defines(@banks) {
    join "\n", gather {
        for @banks -> $b {
            take "/* Op name defines for bank $b.name(). */";
            for $b.ops -> $op {
                take "#define MVM_OP_$op.name() $op.code()";
            }
            take "";
        }
    }
}

# Creates the static array of opcode info.
sub opcode_details(@banks) {
    join "\n", gather {
        for @banks -> $b {
            take "static MVMOpInfo MVM_op_info_{$b.name()}[] = \{";
            for $b.ops -> $op {
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
            take "};";
        }
        take "\nstatic MVMOpInfo *MVM_op_info[] = \{";
        for @banks -> $b {
            take "    MVM_op_info_{$b.name()},";
        }
        take "\};\n";
        take "static unsigned char MVM_op_banks = {+@banks};\n";
        take "static unsigned char MVM_opcounts_by_bank[] = \{";
        for @banks -> $b {
            take "    {+$b.ops},";
        }
        take "\};\n";
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
