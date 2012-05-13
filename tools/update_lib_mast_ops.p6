# This script processes the op list into an NQP source file that lists the
# banks and opcodes.

class Op {
    has $.code;
    has $.name;
}
class Bank {
    has $.name;
    has @.ops;
}

sub MAIN($file = "src/core/oplist") {
    # Parse the ops file and get the various op banks.
    my @banks = parse_ops($file);
    say "Parsed {+@banks} op banks with {[+] @banks>>.ops>>.elems } total ops";

    # Generate file.
    my $hf = open("lib/MAST/Ops.nqp", :w);
    $hf.say("# This file is generated from $file by tools/update_lib_mast_ops.p6.");
    $hf.say("");
    $hf.say(bank_constants(@banks));
    $hf.say(op_constants(@banks));
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
            my ($code, $name, @) = $line.split(/\s+/);
            @banks[$cur_bank].ops.push(Op.new(
                code     => :16($code.substr(2)),
                name     => $name
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
        take 'class MAST::Ops {';
        for @banks>>.ops -> $op {
            take "    our \$$op.name() := $op.code();";
        }
        take '}';
    }
}
