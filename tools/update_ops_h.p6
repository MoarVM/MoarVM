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
    $hf.say("/* This file is generated from $file by tools/update_ops_h.p6. */");
    $hf.say("");
    $hf.say(bank_defines(@banks));
    $hf.say(opcode_defines(@banks));
    $hf.say(opcode_details(@banks));
    $hf.close;
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
                take "    },"
            }
            take "};";
        }
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
