use lib $?FILE.IO.parent.child("lib");

use MAST::Ops;

role CLanguageBase {
    regex ws {
        :r
        [ || '//' \N* \n
          || [ '/*' [<-[*]>+ || '*'<!before '/'>]* '*/' ]
          || [\s|\n]+ ]*
        [ <!before \s> | $ ]
    }

    rule curly_block {
        '{'
         [
           | <curly_block>
           | <-[ { \s / ]> +
         ] *
        '}'
    }
}


# Easiest thing first: op_to_func

sub parse_op_to_func($source) {
    grammar OpToFuncGrammar does CLanguageBase {
        rule TOP {
            .*?
            static void '*' op_to_func '(' MVMThreadContext '*' tc ',' MVMint16 <opcodevar=.ident> ')' '{' # introduction

            switch '(' $<opcodevar> ')' '{'

            <entry>+

            default ':'
            [\N*\n]*?
            '}'
            .*
        }

        rule entry {
            [
                case $<opname>=[ MVM_ <[a..z A..Z 0..9 _]>+ ] ':'
            ]+
            return '&' <funcname=.ident> ';'
            { say "parsed an entry for $<funcname>" }
        }
    }

    my $op_func_table = OpToFuncGrammar.parse($source);

    say "parsed";

    my %result;

    for $op_func_table<entry>.list -> $/ {
        %result{$<opname>>>.Str} = $<funcname>.Str xx *;
    }

    return %result;
}

sub parse_consume_ins_reprops($source) {
    use Grammar::Tracer;
    grammar ConsumeInsGrammar does CLanguageBase {
        rule TOP {
            .*?<?before 'static'>

            <interesting_function>

            .*
        }

        proto rule interesting_function { * }

        multi rule interesting_function:sym<jgb_consume_reprop> {
            'static' 'MVMint32' 'jgb_consume_reprop(' .*? ')' '{'

            # the general structure of this function starts with a big switch
            # statement to find out which operand decides what REPR to look for
            # the C function implementation in.
            # Since we rely on the optree optimizer to do constant folding
            # later on, we don't have to do fact checking manually like the
            # current code does, and so we can just skip the type operand check
            # entirely.

            'switch' '(' 'op' ')' <curly_block>

            # now we'll skip the part where we used to call directly into a
            # repr's functions.

            'if' '(' 'type_facts' .*? ')' <curly_block>

            'switch' '(' op ')' '{'

            <entry>+
            default ':'
            [\N*\n]*?
            '}'
        }

        rule entry {
            .*?

            'break'
            '}'?
        }
    }
    
    my $result = ConsumeInsGrammar.parse($source);

    say $result;
}


sub MAIN {
    my $graph_c_source = slurp($?FILE.IO.parent.parent.child("src").child("jit").child("graph.c"));

    say "got the source";

    my %opcode_to_cfunc = parse_op_to_func($graph_c_source);
    
    parse_consume_ins_reprops($graph_c_source);

}
