use lib $?FILE.IO.parent.child("lib");

use MAST::Ops;

role CLanguageBase {
    regex ws {
        :r
        [ | \s+
          | '//' \N* \n
          | [ '/*' [<-[*]>+ || '*'<!before '/'>]* '*/' ]
        ]*
        [ <!before \s> | $ ]
    }

    regex curly_block {
        '{'
         [
           | \s+
           | <curly_block>
           | <-[ { ]>+
         ]*
        '}'
    }
}


# Easiest thing first: op_to_func

sub parse_op_to_func($source) {
    grammar OpToFuncGrammar does CLanguageBase {
        rule TOP {
            op_to_func '(' MVMThreadContext '*' tc ',' MVMint16 <opcodevar=.ident> ')' '{' # introduction

            'switch(' $<opcodevar>=[<[a..z A..Z 0..9 _]>+] ')' '{'

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
            return '&'? <funcname=.ident> ';'
            { note "parsed an entry for $<funcname>" }
        }
    }

    my $cut_off_source = $source.substr($source.index("op_to_func\(MVMThreadContext"));

    my $op_func_table = OpToFuncGrammar.parse($cut_off_source);

    note "parsed";

    my %result;

    for $op_func_table<entry>.list -> $/ {
        %result{$<opname>>>.Str} = $<funcname>.Str xx *;
    }

    return %result;
}

sub parse_consume_ins_reprops($source, %opcode_to_cfunc) {
    # first, we'll cut the relevant sections of the source out:
    # the part of jgb_consume_reprop after the type-specialized parts
    # and then all of jgb_consume_ins

    my @sourcelines = $source.lines;
    @sourcelines .= grep({ / "couldn't be devirtualized" | " jgb_consume_ins" / ^ff^ / "default:" / });
    @sourcelines .= grep({ $_ !~~ / ^ \s* '/*' .*? '*/' \s* $ / });
    @sourcelines .= grep({ $_ !~~ / ^ \s* $ / });

    # chunkify based on case: and break;
    # we are a very simple parser so if we find a break that's not followed
    # by a new case (or a "}") we just skip ahead until we see the next case.

    my @chunks = gather loop {
        # find the first non-case line.
        my $until = @sourcelines.first-index({ $_ !~~ / "case MVM_".*?':' / });
        my @case-lines = @sourcelines[^$until];
        @sourcelines.shift for ^$until;

        # we'll put all case statements into a single string for easier combing
        my $casestring = [~] @case-lines;
        my @ops = $casestring.comb(/ "case " \s* <( 'MVM_'.*? )> \s* ':' /);

        # find the next case-line.
        $until = @sourcelines.first-index( / "case MVM_".*?':' / );
        $until = +@sourcelines unless $until; # may have to slurp until EOF.
        my @implementationlines = @sourcelines[^$until];
        @sourcelines.shift for ^$until;

        take @ops => @implementationlines;

        last unless @sourcelines;
    }

    # collect everything we've bailed on
    my @skipped_opcodes;

chunkloop: for @chunks.kv -> $chunkidx, $_ {
        my @ops = .key.list;
        my @lines = .value.list;


        # what C variable refers to what piece of the op in the code
        my %var_sources;

        # what arguments do we push to the C stack for this?
        my @c_arguments;

        # keep lines in case we abort somewhere.
        my @lines_so_far;

        # we expect the chunk to begin with some setup:
        # initialise local variables with
        #   register numbers
        #   literal numbers, a string index, ...
        while @lines {
            last if @lines[0] !~~ / ^ \s+ [MVMint|MVMuint] /;
            my $line;

            while ($line = @lines.shift) ~~ m:s/^ [MVMint|MVMuint][16|32|64] <varname=.ident> '='
                    'ins->operands[' $<operandnum>=[\d+] ']'
                    [
                        | $<register>=".reg.orig"
                        | $<lit_str_idx>=".lit_str_idx"
                    ]
                    / {
                @lines_so_far.push: "var_source: $line";
                %var_sources{$<varname>.Str} = $<operandnum>.Int;
            }

            unless $line ~~ m:s/ MVMJitCallArg / {
                note "handling @ops.join(', ')";
                note "this line surprised us (expected MVMJitCallArg):";
                .note for @lines_so_far;
                note $line;
                note "";
                @skipped_opcodes.push: @ops.join(", ");
                next chunkloop;
            }

            # since we consume the line in the condition for the coming
            # loop, but we want to handle this current line there as well,
            # we just unshift it into the lines array again ...
            @lines.unshift($line);

            while ($line = @lines.shift) ~~ m:s/
                ^
                    [MVMJitCallArg args"[]" "=" '{']?
                    [
                    |   '{' <argkind=.ident> ',' [ '{' <argvalue=.ident> '}' | <argvalue=.ident> ]
                    |   '{' $<argkind>="MVM_JIT_LITERAL" ',' '{' $<argvalue>=[\d+] '}'
                    ]
                    [ '}' '}' ';' | '}' ',' ]
                $ / {
                #say $/;
                given $<argkind>.Str {
                    when "MVM_JIT_INTERP_VAR" {
                        @c_arguments.push:
                            "(carg (tc) ptr)";
                    }
                    when "MVM_JIT_REG_VAL" {
                        # later on: figure out if it's a str/obj or an
                        # int register that the op(s) take here.
                        @c_arguments.push:
                            '(carg $' ~ %var_sources{$<argvalue>.Str} ~ " int)";
                    }
                    when "MVM_JIT_LITERAL" {
                        if try $<argvalue>.Int {
                            @c_arguments.push:
                                '(carg (const ' ~ $<argvalue>.Int ~ ' int_sz) int)';
                        } else {
                            note "can't handle literal $<argvalue> yet.";
                            note "can't handle the argument kind $_ yet.";
                            note "handling @ops.join(', ')";
                            note "this line surprised us (expected jgb_append_call_c):";
                            .note for @lines_so_far;
                            note $line;
                            note "";
                            @skipped_opcodes.push: @ops.join(", ");
                            next chunkloop;
                        }
                    }
                    default {
                        note "can't handle the argument kind $_ yet.";
                        note "handling @ops.join(', ')";
                        note "this line surprised us (expected jgb_append_call_c):";
                        .note for @lines_so_far;
                        note $line;
                        note "";
                        @skipped_opcodes.push: @ops.join(", ");
                        next chunkloop;
                    }
                }
                @lines_so_far.push: "c_args: $line";
            }

            unless $line ~~ m:s/ jgb_append_call_c '('
                    tc ',' jgb ',' op_to_func '(' tc ',' op ')' ',' \d+ ',' args ','
                    $<return_type>=[ MVM_JIT_RV_VOID | MVM_JIT_RV_INT | MVM_JIT_RV_PTR ] ','
                    $<return_dst>=[ '-1' | <.ident> ] ')' ';'
                    / {
                note "handling @ops.join(', ')";
                note "this line surprised us (expected jgb_append_call_c):";
                .note for @lines_so_far;
                note $line;
                note "";
                @skipped_opcodes.push: @ops.join(", ");
                next chunkloop;
            }

            my %rv_to_returnkind = (
                    MVM_JIT_RV_VOID => 'void',
                    MVM_JIT_RV_INT  => 'int',
                    MVM_JIT_RV_PTR  => 'ptr',
                );

            for @ops -> $opname {
                note %opcode_to_cfunc{$opname} ~ " going to have a template built for it";

                say "(template: $opname";
                say "    (call (^func {%opcode_to_cfunc{$opname}})";
                say "        (arglist {+@c_arguments}";
                for @c_arguments -> $carg {
                    say "            $carg";
                }
                say "        )";
                say "        " ~ %rv_to_returnkind{$<return_type>};
                say "    ) )";
                say "";
            }
        }
    }

    note "all skipped operations:";
    note "    - $_" for @skipped_opcodes;
}


sub MAIN($graph_c_file? is copy) {
    $graph_c_file //= $?FILE.IO.parent.parent.child("src").child("jit").child("graph.c");
    my $graph_c_source = slurp($graph_c_file);

    note "got the source";

    my %opcode_to_cfunc = parse_op_to_func($graph_c_source);

    parse_consume_ins_reprops($graph_c_source, %opcode_to_cfunc);
}
