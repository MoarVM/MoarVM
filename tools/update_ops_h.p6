# This script processes the op list into a C header file that contains
# info about the opcodes.

class Op {
    has $.code;
    has $.name;
    has @.args;
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
            my ($code, $name, @args) = $line.split(/\s+/);
            @banks[$cur_bank].ops.push(Op.new(
                code => :16($code.substr(2)),
                name => $name,
                args => @args
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
