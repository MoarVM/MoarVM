#!/usr/bin/env perl6

=begin pod

    The Speshlog Excerpt Grapher

    This little script will eat a piece of speshlog (it expects to be fed an
    excerpt that contains only a single Frame with all its BBs, but it'll get
    only slightly confused if BBs are missing) and outputs Graphviz' C<DOT>
    Language to stdout.

    Ideally, you'd use C<dot> to generate an image file from the result or have
    it display the result "interactively" in a window:

    =code
        perl6 graph_spesh.raku a_nice_excerpt.txt | dot -Tsvg > helpful_graph.svg
        perl6 graph_spesh.raku a_nice_excerpt.txt | dot -Tpng > huge_image.png
        perl6 graph_spesh.raku a_nice_excerpt.txt | dot -Tx11

    The -T flag for dot selects the output format. Using K<-Tx11> will open a
    window in which you can pan and zoom around in.

    If you get an error that MAST::Ops could not be found, please run
    C<tools/update_ops.raku> to generate that module.

=end pod

use lib ~$?FILE.IO.parent.child("lib");

use MAST::Ops;

my $current_bb;

my %lit_real_serial;
my %lit_str_serial;

say 'digraph G {';
say '  graph [rankdir="TB"];';
say '  node [shape=record,style=filled,fillcolor=white];';

say "    \"Control Flow Graph\";";
say "    \"Dominance Tree\";";

my $insnum = 0;
my $in_subgraph = 0;

my $ann_num = 0;

# if instruction-carrying lines appear before the first BB, we
# shall gracefully invent a starting point.
my $last_ins = "\"out of nowhere\"";

my @comments_queued;

my %bb_map;

my %bb_fh_gotos;

my @connections;
my %bb_connections;

my @dominance_conns;

my @callsite_args;

my %reg_writers;
my @delayed_writer_connections;

my @bb_overview;

constant @bb_colors = ((((1 .. *) X* 0.618033988749895) X% 1.0) .map(*.fmt("%.5f "))
                      Z~ (((0, -1 ... *) X* 0.0618033988749895) X% 0.05 + 0.95) .map(*.fmt("%.5f ")))
                      X~ "0.9900";

my $unparsed_line_idx = 0;
for lines() -> $_ is copy {
    when / ^ '      ' <!before '['> $<opname>=[<[a..z I 0..9 _]>+] \s+
            [ $<argument>=[
              | r \s* $<regnum>=[<.digit>+] \s* '(' \s* $<regver>=[<.digit>+] \s* ')'
              | "lit" u? "i" <.digit>+ '(' ~ ')' <-[)]>+
              | litn <.digit>+ '(' ~ ')' <-[)]>+
              | lits '(' .*? ')'
              | lex '(' .*? ')'
              | sslot '(' <digit>+ ')'
              | BB '(' <digit>+ ')'
              | coderef '(' ~ ')' [<-[)]>+ " (closure)" ]
              | callsite '(' ~ ')' <-[)]>+
              | '<nyi>'
              | '<nyi(lit)>'
            ] ]* % [',' \s*]
	    [\s* '(' <-[)]>+ ')']? # i don't remember what this used to be for?
	    \s*
	    ["# " "[" ~ "]" $<commentnum>=[<-[\]]>+] $<commenttext>=[\N+]]?
	    $ / {
        say "";
        say "    \"{$<opname>}_{$insnum}\" ";
        print "    [";

        my $opname = $<opname>.Str;
        if $opname eq "set" | "decont" {
            print "shape=Mrecord ";
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
                slip do for 1..^$arity { $%(
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
                    @delayed_writer_connections.push: $v => $current_ins ~ ":$k";
                }
            }
            @labelparts.push: "<$k> $v";
        }

        if $arity && @props[0]<writes_tgt> {
            @labelparts = flat @labelparts[1, 0], @labelparts[2..*];
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

        say "    ];";

        if $<commentnum> && $<commenttext> {
            @comments_queued.push: %( num => $<commentnum>.Str, text => $<commenttext>.Str );
        }

        for @comments_queued.rotor(2=>-1, :partial) -> ($prev_c, $next_c?) {
            say "        \"comment_$($prev_c<num>)\" [shape=Mrecord fillcolor=lightgrey label=\"$prev_c<num> | $($prev_c<text>)\"];";
            with $next_c {
                say "        \"comment_$($prev_c<num>)\" -> \"comment_$($next_c<num>)\";";
            }
            else {
                say "        \"comment_$($prev_c<num>)\" -> \"{$opname}_{$insnum}\";";
            }
        }

        @comments_queued = Empty;

        $insnum++;

        say "";
        if $previous_ins ~~ / entry / {
            say "    $previous_ins -> $last_ins [style=dotted];";
        } else {
            say "    $previous_ins -> $last_ins [color=\"#999999\"];";
        }
        say "";

        for @back_connections {
            say "      $_.key() -> $_.value();";
        }
        say "";
        say "";
    }
    when / ^ '      # ' '[' ~ ']' $<commentnum>=[<-[\]]>+] \s* $<commenttext>=[\N+] $ / {
        @comments_queued.push: %( num => $<commentnum>.Str, text => $<commenttext>.Str );
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
        say "    style=filled;";
        say "    color=\"@bb_colors[+$<bbnum>]\";";
        say "    rankdir = TB;";
        #say "    label = \"$<bbnum>\";";
        say "    \"entry_$<addr>\" [label=\"<op> entry of block $<bbnum>\"];";
        $in_subgraph = True;
        $current_bb = ~$<addr>;
        $last_ins = "\"entry_$<addr>\"";

        @bb_overview.push: %( addr => $<addr>, bbnum => +$<bbnum> );
    }
    when / ^ '    ' 'Successors: ' [$<succ>=[<.digit>+]]* % ', ' $ / {
        %bb_connections{$current_bb} = @<succ>>>.Str;
    }
    when / ^ '      ' '[Annotation: ' $<annotation>=[<[a..z A..Z 0..9 \ ]>+] $<rest>=<-[\]]>+ / {
        my $previous_ins = $last_ins;
        $last_ins = "\"annotation_{$current_bb}_{$<annotation>}_{$ann_num++}\"";
        my $rest = $<rest>;
        say "    $last_ins [label=\"{$<annotation>} {$rest}\" shape=cds];";
        if $<annotation> ~~ / "FH Goto" / {
            %bb_fh_gotos{$current_bb}.push: ~$rest.comb(/\d+/);
        }
        if $last_ins ~~ / entry / {
            say "    $previous_ins -> $last_ins [style=dotted];";
        } else {
            say "    $previous_ins -> $last_ins [color=lightgrey];";
        }
    }
    when / ^ 'Finished specialization of ' / { }
    when / ^ '    ' \s* r $<regnum>=[<.digit>+] '(' $<regver>=[<.digit>+] ')' ':' / { }
    when / ^ '    ' 'Dominance children: ' [$<child>=[<.digit>+]]* % [',' <.ws>] / {
        for $<child>.list -> $child {
            @dominance_conns.push($current_bb => $child.Int);
        }
    }
    when / ^ '    ' [ 'Instructions' | 'Predecessors' ] / { }
    when /^ [ 'Facts' | '='+ ] / { }
    when /^ 'Spesh of \'' $<methname>=[<[a..z 0..9 _ ' -]>*]
            '\' (cuid: ' $<cuid>=[<[a..z A..Z 0..9 _ . -]>+]
            ', file: ' $<filename>=[<-[:]>*] ':' $<lineno>=[<digit>+] ')' $ / {
        say "    file [shape=record label=\"\{ {$<methname>} | {$<filename>}:{$<lineno>} | {$<cuid>} \}\"];";
    }
    when / ^ \s* $ / { }
    when /^ 'Callsite ' $<uniqid>=[<[a..f A..F 0..9 x]>+] ' (' $<argc>=[<digit>+] ' args, ' $<posc>=[<digit>+] ' pos)' $/ {
        say "    callsite [shape=record label=\"\{ Callsite | {$<argc>} arguments, {$<posc>} of them positionals | {$<uniqid>} \}\"];";
    }
    when / ^ '  - ' $<argument_name>=[<[a..z A..B 0..9 _ ' -]>+] $ / {
        @callsite_args.push: ~$<argument_name>;
    }
    when / ^ '      PHI' / {
        # we don't have a nice way to show PHI nodes yet, sadly.
    }
    when / ^ ['Stats:' | 'Logged values:'] / { }
    when / ^ '    ' \d+ [ 'spesh slots' | 'log values'] / { }
    when / ^ '    ' \s* [\d+]+ %% \s+ / { }
    default {
        say "    unparsed_line_$($unparsed_line_idx++) [label=\"{$_}\"];";
    }
}

say "  }" if $in_subgraph;

if @callsite_args {
    say @callsite_args.map({ "\"arg_$((state $)++)\" [label=\"$_\"]" }).join(';');
    say "callsite -> " ~ (^@callsite_args).map({"\"arg_$_\""}).join(" -> ") ~ ";";
}

for @connections {
    say "$_.<source_ins> -> \"entry_{ %bb_map{.<target_block>} }\" [style=dotted];";
}

for @dominance_conns {
    say "\"exit_$_.key()\" -> \"entry_%bb_map{$_.value}\" [style=tapered;penwidth=10;arrowhead=none;color=grey];";
    say "\"bb_ov_d_$_.key()\" -> \"bb_ov_d_%bb_map{$_.value}\" [style=tapered;penwidth=10;arrowhead=none;color=grey];";
    once say "\"Dominance Tree\" -> \"bb_ov_d_$_.key()\";";
}

for @delayed_writer_connections -> $conn {
    my $from = $conn.key;
    my $to   = $conn.value;

    if %reg_writers{$from}:exists {
        say "    %reg_writers{$from} -> $to;";
        note "found a connection for $from even after reading the whole file ...";
    } else {
        note "Couldn't find a writer for $from anywhere! (harmless error)";
    }
}

for %bb_connections.kv -> $k, $v {
    # Make a little flag at the 0 BB to show that this is the CFG
    if %bb_map{$k} == 0 { say "\"Control Flow Graph\" -> \"bb_ov_$k\";" }
    # bb 0 is special and has successors that it won't jump to.
    #note "marking successors for block $k";
    #note $v.perl;
    #note "";
    next unless @$v;
    #my @candidates = do %bb_map{$k} == "0"
    #    ?? %bb_map{@$v}
    #    !! %bb_map{$v[*-1]};
    my @candidates = %bb_map{@$v};
    for @candidates.kv -> $i, $cand {
        say "    \"exit_$k\" -> \"entry_$cand\" [style=dotted];";
        say "    \"bb_ov_$k\" -> \"bb_ov_$cand\"$( $i == 0 ?? "" !! "[style=dashed]" );";
    }
}

for @connections {
    say "    \"bb_ov_$($_.<source_block>)\" -> \"bb_ov_$(%bb_map{.<target_block>} )\" [style=dotted];";
}

for @bb_overview -> $/ {
    if %bb_fh_gotos{$<addr>} -> $fhs {
        my $label = $fhs.map("FH " ~ *).join(" | ") ~ " | " ~ $<bbnum>;
        say "    \"bb_ov_$<addr>\" [fillcolor=\"@bb_colors[+$<bbnum>]\",color=black,style=filled,shape=Mrecord,label=\"$label\"];";
    } else {
        say "    \"bb_ov_$<addr>\" [fillcolor=\"@bb_colors[+$<bbnum>]\",color=black,style=filled,label=\"$<bbnum>\"];";
    }
    say "    \"bb_ov_d_$<addr>\" [fillcolor=\"@bb_colors[+$<bbnum>]\",color=black,style=filled,label=\"$<bbnum>\"];";
}

say '}';

