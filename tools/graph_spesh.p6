use lib $?FILE.path.parent.child("lib");

use MAST::Ops;

my $current_bb;

my %lit_real_serial;
my %lit_str_serial;

say 'digraph G {';
say '  graph [rankdir="TB"];';
say '  node [shape=record]';

my $insnum = 0;
my $in_subgraph = 0;

# if instruction-carrying lines appear before the first BB, we
# shall gracefully invent a starting point.
my $last_ins = "\"out of nowhere\"";

my %bb_map;

my @connections;
my %bb_connections;

my %reg_writers;

for lines() :eager -> $_ is copy {
    when / ^ '      ' <!before '['> $<opname>=[<[a..z 0..9 _]>+] \s+
            [ $<argument>=[
              | r $<regnum>=[<.digit>+] '(' $<regver>=[<.digit>+] ')'
              | liti <.digit>+ '(' ~ ')' <-[)]>+
              | litn <.digit>+ '(' ~ ')' <-[)]>+
              | lits '(' .*? ')'
              | lex '(' .*? ')'
              | sslot '(' <digit>+ ')'
              | BB '(' <digit>+ ')'
              | '<nyi>'
              | '<nyi(lit)>'
            ] ]* % ', ' \s* $ / {
        say "";
        print "    \"{$<opname>}_{$insnum}\" ";
        say "    [";

        if $<opname> eq "set" | "decont" {
            say "       shape=Mrecord";
        }

        my $previous_ins = $last_ins;
        my $current_ins = "\"{$<opname>}_{$insnum}\"";
        $last_ins = $current_ins ~ ":op";

        my @back_connections;

        my @labelparts = qq[ <op> $<opname>  ];

        #note "---------------";
        #note @<argument>.gist;
        #note "---------------";

        my @props;
        my $opcode;
        my $arity;

        if %MAST::Ops::codes{$<opname>}:exists {
            $opcode = %MAST::Ops::codes{$<opname>};
            $arity  = @MAST::Ops::counts[$opcode];
            my $offset = @MAST::Ops::offsets[$opcode];

            @props = do for ^$arity {
                $%(
                    flags    => (my $flags    = @MAST::Ops::values[$offset + $_]),
                    rwmasked => (my $rwmasked = $flags +& %MAST::Ops::flags<MVM_operand_rw_mask>),
                    type     => ($flags +& %MAST::Ops::flags<MVM_operand_type_mask>),
                    is_sslot => ($flags +& %MAST::Ops::flags<MVM_operand_spesh_slot>),

                    targets_reg => ($rwmasked +& (%MAST::Ops::flags<MVM_operand_write_reg>
                                         +| %MAST::Ops::flags<MVM_operand_read_reg>)),
                    writes_tgt  => ($rwmasked +& (%MAST::Ops::flags<MVM_operand_write_reg>
                                         +| %MAST::Ops::flags<MVM_operand_write_lex>)),
                )
            }
        } else {
            # we have an extop here. assume it writes to its first register and
            # has exactly as many arguments as it says in the spesh log.
            $arity = @<argument>.elems;

            @props = $%(
                    flags => 0,
                    rwmasked => (my $type = %MAST::Ops::flags<MVM_operand_write_reg>),
                    type => $type,
                    targets_reg => 1,
                    writes_tgt => 1 ),
                do for 1..^$arity { $%(
                    flags => 0,
                    rwmasked => (my $boringtype = %MAST::Ops::flags<MVM_operand_read_reg>),
                    type => $boringtype,
                    targets_reg => @<argument>[$_].match(/r<digit>+'('<digit>+')'/) ?? 1 !! 0,
                    writes_tgt => 0 ) };
        }

        my @argument_names = @<argument>>>.Str>>.trans( "<" => "«", ">" => "»" );

        if $arity && @props[0]<writes_tgt> {
            if @props[0]<targets_reg> {
                %reg_writers{@argument_names[0]} = $current_ins ~ ":0";
            }
        }

        my $first_read = @props[0]<writes_tgt> ?? 1 !! 0;
        for @argument_names.kv -> $k, $v {
            if $k >= $first_read and @props[$k]<targets_reg> {
                if %reg_writers{$v}:exists {
                    @back_connections.push: %reg_writers{$v} => $current_ins ~ ":$k";
                } else {
                    note "found register without writer: $v for instruction $current_ins argument $k"
                }
            }
            @labelparts.push: "<$k> $v";
        }

        if $arity && @props[0]<writes_tgt> {
            @labelparts = @labelparts[1, 0], @labelparts[2..*];
        }

        # find outgoing connections

        for @argument_names.kv -> $k, $_ {
            if m/ BB '(' $<target>=[<digit>+] ')' / -> $/ {
                @connections.push: $%(
                    source_block => $current_bb,
                    target_block => $<target>,
                    source_ins   => $current_ins ~ ":<$k>"
                );
            }
        }

        print "    label=\"{ @labelparts.join(" | ") }\" rank=$insnum";

        $insnum++;

        say "    ];";

        say "";
        if $previous_ins ~~ / entry / {
            say "    $previous_ins -> $last_ins [style=dotted];";
        } else {
            say "    $previous_ins -> $last_ins [color=lightgrey];";
        }
        say "";

        for @back_connections {
            say "      $_.key() -> $_.value();";
        }
        say "";
        say "";
    }
    when / ^ '  BB ' $<bbnum>=[<.digit>+] ' (' ~ ')' $<addr>=<[0..9 a..f x]>+ ':' $ / {
        %bb_map{~$<bbnum>} = ~$<addr>;
        %bb_map{~$<addr>} = ~$<bbnum>;
        if $in_subgraph {
            say "    \"exit_$current_bb\";";
            say "    $last_ins -> \"exit_$current_bb\" [style=dotted];";
            say "  }" if $in_subgraph;
        }
        say "  subgraph ";
        say "\"cluster_{~$<addr>}\" \{";
        say "    rankdir = TB;";
        #say "    label = \"$<bbnum>\";";
        say "    \"entry_$<addr>\" [label=\"<op> entry of block $<bbnum>\"];";
        $in_subgraph = True;
        $current_bb = ~$<addr>;
        $last_ins = "\"entry_$<addr>\"";
    }
    when / ^ '    ' 'Successors: ' [$<succ>=[<.digit>+]]+ % ', ' $ / {
        %bb_connections{$current_bb} = @<succ>>>.Str;
    }
    when / ^ '      ' '[Annotation: ' $<annotation>=[<[a..z A..Z 0..9 \ ]>+] / {
        my $previous_ins = $last_ins;
        $last_ins = "\"annotation_{$current_bb}_{$<annotation>}_{(state $)++}\"";
        say "    $last_ins [label=\"{$<annotation>}\" shape=cds];";
        if $last_ins ~~ / entry / {
            say "    $previous_ins -> $last_ins [style=dotted];";
        } else {
            say "    $previous_ins -> $last_ins [color=lightgrey];";
        }
    }
    when / ^ '    ' r $<regnum>=[<.digit>+] '(' $<regver>=[<.digit>+] ')' ':' / {
        
    }
}

say "  }" if $in_subgraph;

for @connections {
    say "$_.<source_ins> -> \"entry_{ %bb_map{.<target_block>} }\" [style=dotted];";
}

for %bb_connections.kv -> $k, $v {
    # bb 0 is special and has successors that it won't jump to.
    #note "marking successors for block $k";
    #note $v.perl;
    #note "";
    my @candidates = do %bb_map{$k} == "0"
        ?? %bb_map{@$v}
        !! %bb_map{$v[*-1]};
    for @candidates -> $cand {
        say "\"exit_$k\" -> \"entry_$cand\" [style=dotted];";
    }
}

say '}';

