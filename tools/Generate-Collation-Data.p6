#!/usr/bin/env perl6
use lib <lib tools/lib>;
use Collation-Gram;
use ArrayCompose;
my $my_debug = False;
# Set this to only generate a partial run, for testing purposes
my Int $less-than;
my $out-file = "src/strings/unicode_uca.c";
class p6node {
    has Int $.cp;
    has @!collation_elements;
    has $!last;
    has %.next is rw;
    method next-cps                           { %!next.keys.map(*.Int).sort  }
    method has-collation                      { @!collation_elements.Bool    }
    method get-collation                      { @!collation_elements }
    method set-collation (Positional:D $list) {
        @!collation_elements = |$list;
    }
    method set-cp (Int:D $cp) { $!cp = $cp }
}
sub p6node-find-node (Int:D $cp, p6node $p6node is rw --> p6node) is rw {
    die unless $p6node.next{$cp}.VAR.^name eq 'Scalar';
    die "can't find the node for $cp " unless $p6node.next{$cp}.isa(p6node);
    return-rw $p6node.next{$cp} orelse die "Can't find node";
}
sub p6node-create-or-find-node (Int:D $cp, p6node:D $p6node is rw) is rw {
    my $hash := $p6node.next;
    #say "p6node-create-or-find-node called for cp $cp";
    if $hash{$cp}:exists {
        return-rw $p6node.next{$cp};
    }
    else {
        my $obj = p6node.new(cp => $cp, last => $hash);
        $obj.set-cp($cp);
        $hash{$cp} = $obj;
        return-rw $hash{$cp};
    }

}
sub print-var ($var) { $var.gist }
my Str $Unicode-Version;
my @implicit-weights;
my $max-cp = 0;
sub int-bitwidth (Int:D $int) {
    $int.base(2).chars + 1;
}
sub uint-bitwidth (Int:D $int) {
    $int.base(2).chars;
}
my Int:D $codepoint_sequence_no_max = 0;
sub parse-test-data (p6node:D $main-p6node) {
    my $data = "UNIDATA/UCA/allkeys.txt".IO;
    my $line-no;
    for $data.lines -> $line {
        $line-no++;
        last if $less-than and 10_000 < $less-than;
        #say $line-no;
        next if $line eq '' or $line.starts-with('#');
        if $line.starts-with('@version') {
            $Unicode-Version = $line.subst('@version ', '');
            next;
        }
        if $line.starts-with('@implicitweights') {
            @implicit-weights.push: $line.subst('@implicitweights ', '');
            next;
        }
        my $var = Collation-Gram.new.parse($line, :actions(Collation-Gram::Action.new)).made;
        die $line unless $var;
        # skip them if it's not a sequence (only one codepoint), AND there
        # is only one collation element. These are picked up into the main MVM
        # UCD database
        next if $var<codepoints>.elems == 1 && $var<array>.elems == 1;
        my $node = $main-p6node;
        say $line, "\n", $var<codepoints> if $my_debug;
        $codepoint_sequence_no_max = $var<codepoints>.elems
            if $codepoint_sequence_no_max < $var<codepoints>.elems;
        for $var<codepoints>.list -> $cp {
            $max-cp = $cp if $max-cp < $cp;
            $node = p6node-create-or-find-node($cp, $node);
        }
        $node.set-collation($var<array>);
    }
    say "Done with parse-test-data";
}

class sub_node {
    has Int $.codepoint;
    has Int $.sub_node_elems      is rw ;
    has Int $.sub_node_link       is rw;
    has Int $.collation_key_elems is rw = 0;
    has Int $.collation_key_link  is rw = 0;
    has Int $.element             is rw;
    method build {
        $!codepoint,
        $!sub_node_elems,
        # To save space, set to zero if it's -1 (-1 means there's no link)
        # we can determine there is no link by checking the collation_key_elems
        # or sub_node_elems
        # so we don't need to set these to -1
        ($!sub_node_link == -1 ?? 0 !! $!sub_node_link),
        $!collation_key_elems,
        ($!collation_key_link == -1 ?? 0 !! $!collation_key_link),
    }
    method Str {
        "\{{$.codepoint.fmt("0x%X")}, $!sub_node_elems, $!sub_node_link, $!collation_key_elems, $!collation_key_link\}"
    }
}
#| Adds the initial codepoint nodes to @main-node
sub add-main-node-to-c-data (p6node:D $p6node is rw, @main-node) is rw {
    for $p6node.next.keys.map(*.Int).sort -> $cp {
        my $thing := sub_node.new(codepoint => $cp, element => @main-node.elems);
        @main-node.push: $thing;
    }
    @main-node.elems;
}

#say Dump @main-node;
#| Follows the codepoints already in @main-node and adds sub_nodes based on that
sub sub_node-flesh-out-tree-from-main-node-elems
(p6node:D $main-p6node is rw, @main-node, @collation-elements) {
    for ^@main-node -> $i {
        #say "Processing $sub_node.codepoint()";
        sub_node-add-to-c-data-from-sub_node(@main-node[$i],
            p6node-find-node(@main-node[$i].codepoint, $main-p6node),
            @main-node, @collation-elements);
    }
}
sub sub_node-add-to-c-data-from-sub_node
(sub_node:D $sub_node is rw, p6node:D $p6node is rw, @main-node, @collation-elements --> sub_node:D) is rw {
    die unless $sub_node.codepoint == $p6node.cp;
    if $p6node.has-collation {
        my $temp := sub_node-add-collation-elems-from-p6node($sub_node, $p6node, @collation-elements);
        die "\$temp !=== \$sub_node" unless $temp === $sub_node;
    }
    #if !$sub_node.sub_node_elems {
    $sub_node.sub_node_elems = $p6node.next.elems;
    #}
    #die "\$sub_node.sub_node_elems !== \$p6node.next.elems" unless $sub_node.sub_node_elems == $p6node.next.elems;
    my Int ($last-link, $first-link) = -1 xx 2;
    for $p6node.next-cps -> $cp {
        $last-link = sub_node-add-sub_node($cp, @main-node);
        sub_node-add-to-c-data-from-sub_node(@main-node[$last-link], p6node-find-node($cp, $p6node), @main-node, @collation-elements);
        $first-link = $last-link if $first-link == -1;
    }
    $sub_node.sub_node_link = $first-link;

    #say Dump $sub_node;
    $sub_node;
}
sub sub_node-add-sub_node (Int:D $cp, @main-node --> Int:D) {
    my $node := sub_node.new(codepoint => $cp, element => @main-node.elems);
    die "!\$node.element.defined || !\$node.codepoint.defined"
        unless $node.element.defined && $node.codepoint.defined;
    @main-node.push: $node;
    return @main-node.elems - 1;
}
my Int:D $max-collation-elems = 0;
my Int:D $max-primary   = 0;
my Int:D $max-secondary = 0;
my Int:D $max-tertiary  = 0;
my Int:D $max-special   = 0;
sub sub_node-add-collation-elems-from-p6node (sub_node:D $sub_node is rw, p6node:D $p6node is rw, @collation-elements --> sub_node:D) is rw {
    die "!\$p6node.has-collation" unless $p6node.has-collation;
    my Int:D $before-elems = @collation-elements.elems;
    for $p6node.get-collation <-> $element {
        $max-primary   = $element[0] if $max-primary   < $element[0];
        $max-secondary = $element[1] if $max-secondary < $element[1];
        $max-tertiary  = $element[2] if $max-tertiary  < $element[2];
        $max-special   = $element[3] if $max-special   < $element[3];
        @collation-elements.push: $element;
    }
    $max-collation-elems = $p6node.get-collation.elems if $max-collation-elems < $p6node.get-collation.elems;
    my Int:D $after-elems = @collation-elements.elems;
    $sub_node.collation_key_link  = $before-elems;
    $sub_node.collation_key_elems = $after-elems - $before-elems;
    $sub_node;
}
my @main-node;
my @collation-elements;
my $main-p6node = p6node.new;
sub debug-out-nodes {
    use JSON::Fast;
    spurt 'out_nodes', to-json(@main-node.map(*.build));
}
sub process-block (Str:D $text) {
    if $text ~~ / ^ \s* $<fullnam>=( $<start>=(<:AHex>+) ['..' $<end>=(<:AHex>+)]? \s* ';' \s* $<name>=(.*) ) \s* $ / {
        #.say;
        my Int:D $start = $<fullnam><start>.Str.parse-base(16);
        my Int:D $end = $<fullnam><end> ?? $<fullnam><end>.Str.parse-base(16) !! $start;
        my Str:D $name = $<fullnam><name>.Str;
        my Str:D $fullname = $<fullnam>.Str;
        return $start, $end, $name, $fullname;
    }
    else {
        die;
    }

}
sub get-block-data (Str:D $file, @looking, $funcname) {
    die unless "UNIDATA/$file".IO.f;
    my $myfile = slurp "UNIDATA/$file";
    my @out = "/* Data from $file */", "MVM_STATIC_INLINE MVMuint32 $funcname " ~ '(MVMCodepoint cp) {';
    @out.push: 'return'.indent: 4;
    my Int:D $num = 0;
    for $myfile.lines {
        next if /^ \s* '#' /;
        next if /^ \s* $/;
        if ($file eq 'PropList.txt') {
            my @split = .split(/[\s+|';']/, :skip-empty);
            #say @split.perl;
            next unless @split[1].trim eq @looking.any;
        }
        if ($file eq 'Blocks.txt') {
            my $found = False;
            for @looking -> $looking {
                $found = True if m/^\s* <:AHex>+ '..' <:AHex>+ \s* ';' \s* $looking \s* $/;
            }
            next unless $found;
        }
        #100000..10FFFF; Supplementary Private Use Area-B
        #(0x3400 <= cp && cp <= 0x4DB5) /*  3400..4DB5  d*/
        my ($start, $end, $name, $fullname) = process-block $_;

        my Str:D $or = $num++ ?? '||' !! '  ';
        #say $num;
        my Str:D $conditional = $start == $end
            ?? "0x%-22X == cp".sprintf($start)
            !! "0x%-5X <= cp && cp <= 0x%-5X".sprintf: $start, $end;
        @out.push: "%s (%s) /* %4X..%-4X %-34s */".sprintf($or, $conditional, $start, $end, $name).indent: 4;
        #say "start $start end $end name: “$name”";
    }
    @out.push: ';'.indent: 4;
    @out.push: '}';
    @out.join("\n") ~ "\n";

}
parse-test-data($main-p6node);
my $main-node-elems = add-main-node-to-c-data($main-p6node, @main-node);
sub_node-flesh-out-tree-from-main-node-elems($main-p6node, @main-node, @collation-elements);
say now - INIT now;
sub format-collation-Str ($a) {
    my Str $out;
    for $a -> $item is copy {
        my $thing = $item.pop;
        my Str:D $thing-str = $thing ?? '*' !! '.';
        $out ~= "[%s%.4X.%.4X.%.4X]".sprintf($thing-str, |$item);
    }
    $out;
}
my @composed-arrays = "/* This file generated from tools/Generate-Collation-Data.p6 */";
sub make-struct (@names, @types, @collation-list-for-packing, $struct-name) {
    use lib 'lib';
    use BitfieldPacking; use bitfield-rows-switch;
    my @order = compute-packing(@collation-list-for-packing);
    my @out-str = "struct $struct-name \{";
    for @order -> $pair {
        @out-str.push: ([~] @types[$pair.key], " ", @names[$pair.key], " :", $pair.value, ";").indent(4);
    }
    @out-str.push: '};';
    @out-str.join("\n"), @order;
}
my @collation-list-for-packing =
    0 => uint-bitwidth($max-primary),
    1 => uint-bitwidth($max-secondary),
    2 => uint-bitwidth($max-tertiary),
    3 => uint-bitwidth($max-special);
my @collation_key_names =
    'primary', 'secondary', 'tertiary', 'special';
my ($collation_key_struct, $collation_key_order) = make-struct(
    @collation_key_names,
    ("MVMuint32" xx 4),
    @collation-list-for-packing,
    'collation_key');
@composed-arrays.push: $collation_key_struct;
my @names2 = <codepoint sub_node_elems sub_node_link
              collation_key_elems collation_key_link>;
my @sub_node-list-for-packing2 =
    0 => uint-bitwidth($max-cp),
    1 => uint-bitwidth(@main-node.elems - 1),
    2 => uint-bitwidth(@main-node.elems - 1),
    3 => uint-bitwidth($max-collation-elems),
    4 => uint-bitwidth(@collation-elements.elems - 1);
my ($sub_node_struct, $order2) = make-struct(
    @names2,
    ('MVMuint32' xx 5),
    @sub_node-list-for-packing2,
    'sub_node');
@composed-arrays.push: $sub_node_struct;
@composed-arrays.push: "typedef struct sub_node sub_node;";
sub transform-array (@array, @order) {
    @array.map(-> $item {
        my @out;
        for ^$item.elems -> $i {
            @out[$i] = $item[@order[$i].key];
        }
        @out;
    });
}
@composed-arrays.push: "#define main_nodes_elems @main-node.elems()";
@composed-arrays.push: "#define starter_main_nodes_elems $main-node-elems";
@composed-arrays.push: "#define codepoint_sequence_no_max $codepoint_sequence_no_max";
@composed-arrays.push: "#define special_collation_keys_elems @collation-elements.elems()";
@composed-arrays.push: get-block-data("PropList.txt", ("Unified_Ideograph",), "is_unified_ideograph");
@composed-arrays.push: get-block-data("Blocks.txt", ("Nushu",), "is_Assigned_Block_Nushu");
@composed-arrays.push: get-block-data("Blocks.txt", ("Tangut","Tangut Components"), "is_Block_Tangut");
@composed-arrays.push: get-block-data("Blocks.txt", ("CJK Unified Ideographs","CJK Compatibility Ideographs"), "is_Block_CJK_Unified_Ideographs_OR_CJK_Compatibility_Ideographs");
@composed-arrays.push: compose-array('sub_node', 'main_nodes', transform-array(@main-node».build, $order2));
@composed-arrays.push: compose-array( 'struct collation_key', 'special_collation_keys', transform-array(@collation-elements, $collation_key_order));
spurt $out-file, @composed-arrays.join("\n");
print qq:to/END/;
Done writing $out-file.
{'=' x 70}
MAKE SURE TO RUN `tools/CollationTest.t` to ensure there are ~74 failures only!
END
