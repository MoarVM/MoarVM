#!/usr/bin/env perl6
use lib <lib tools/lib>;
use Collation-Gram;
use ArrayCompose;
#use UCDlib;
my $my_debug = False;
# Set this to only generate a partial run, for testing purposes
my Int $less-than;
my $test-data = Q:to/END/;
0F68  ; [.2E6C.0020.0002] # TIBETAN LETTER A
0F00  ; [.2E6C.0020.0004][.2E83.0020.0004][.0000.00C4.0004] # TIBETAN SYLLABLE OM
0FB8  ; [.2E6D.0020.0002] # TIBETAN SUBJOINED LETTER A
0F88  ; [.2E6E.0020.0002] # TIBETAN SIGN LCE TSA CAN
0F8D  ; [.2E6F.0020.0002] # TIBETAN SUBJOINED SIGN LCE TSA CAN
0F89  ; [.2E70.0020.0002] # TIBETAN SIGN MCHU CAN
0F8E  ; [.2E71.0020.0002] # TIBETAN SUBJOINED SIGN MCHU CAN
0F8C  ; [.2E72.0020.0002] # TIBETAN SIGN INVERTED MCHU CAN
0F8F  ; [.2E73.0020.0002] # TIBETAN SUBJOINED SIGN INVERTED MCHU CAN
0F8A  ; [.2E74.0020.0002] # TIBETAN SIGN GRU CAN RGYINGS
0F8B  ; [.2E75.0020.0002] # TIBETAN SIGN GRU MED RGYINGS
0F71  ; [.2E76.0020.0002] # TIBETAN VOWEL SIGN AA
0F72  ; [.2E77.0020.0002] # TIBETAN VOWEL SIGN I
0F73  ; [.2E78.0020.0002] # TIBETAN VOWEL SIGN II
0F71 0F72 ; [.2E78.0020.0002] # TIBETAN VOWEL SIGN II
0F80  ; [.2E79.0020.0002] # TIBETAN VOWEL SIGN REVERSED I
0F81  ; [.2E7A.0020.0002] # TIBETAN VOWEL SIGN REVERSED II
AAB5 AA87 ; [.2DEA.0020.0002][.2E18.0020.0002] # <TAI VIET VOWEL E, TAI VIET LETTER HIGH GO>
END
#`(class collation_key {
    has Int:D $.primary   is rw;
    has Int:D $.secondary is rw;
    has Int:D $.tertiary  is rw;
    has Int:D $.special   is rw;
})
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
        #$line ~~ / ^ [ $<codes>=( <:AHex>+ )+ % \s+ ] \s* ';' .* $ / or next;
        my $var = Collation-Gram.new.parse($line, :actions(Collation-Gram::Action.new)).made;
        die $line unless $var;
        next if $var<codepoints>.elems == 1 && $var<array>.elems == 1;
        say "Adding data for cp $var<codepoints>[0]" if $var<codepoints>.any == 183;
        my $node = $main-p6node;
        say $line, "\n", $var<codepoints> if $my_debug;
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
    has Int $.collation_key_elems is rw =  0;
    has Int $.collation_key_link  is rw = -1;
    has Int $.element             is rw;
    method build {
        $!codepoint,
        $.sub_node_elems,
        $.sub_node_link,
        $.collation_key_elems,
        $.collation_key_link,
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
    #say "Adding collation data for $sub_node.codepoint()";
    $sub_node;
}
my @main-node;
my @collation-elements;
my $main-p6node = p6node.new;
sub debug-out-nodes {
    use JSON::Fast;
    spurt 'out_nodes', to-json(@main-node.map(*.build));
}
parse-test-data($main-p6node);
use Data::Dump;
#say Dump $main-p6node;
my $main-node-elems = add-main-node-to-c-data($main-p6node, @main-node);
#say Dump $main-p6node;
#exit;
sub_node-flesh-out-tree-from-main-node-elems($main-p6node, @main-node, @collation-elements);
#say Dump $main-p6node;
#say Dump @main-node;
say now - INIT now;
multi sub test-it (Str:D $str) {
    test-it $str.split(' ')».parse-base(16);
}
multi sub test-it (@list) {
    for ^$main-node-elems -> $i {
        if @main-node[$i].codepoint == @list[0] {
            say "First node";
            say @main-node[$i].Str;
            say "Second node";
            say @main-node[@main-node[$i].sub_node_link];
            say "Collation linked";
            say format-collation-Str @collation-elements[@main-node[@main-node[$i].sub_node_link].collation_key_link];
            say "done";
        }
    }
}
sub format-collation-Str ($a) {
    my Str $out;
    for $a -> $item is copy {
        my $thing = $item.pop;
        my Str:D $thing-str = $thing ?? '*' !! '.';
        $out ~= "[%s%.4X.%.4X.%.4X]".sprintf($thing-str, |$item);
    }
    $out;
}
my @composed-arrays;
my $struct = qq:to/END/;
struct sub_node \{
    unsigned int codepoint :{uint-bitwidth($max-cp)};
    unsigned int sub_node_elems :{uint-bitwidth(@main-node.elems - 1)};
    int sub_node_link :{int-bitwidth(@main-node.elems -1)};
    unsigned int collation_key_elems :{uint-bitwidth($max-collation-elems)};
    int collation_key_link :{int-bitwidth(@collation-elements.elems - 1)};
\};
typedef struct sub_node sub_node;
END

say "Max primary: $max-primary max secondary $max-secondary max tertiary $max-tertiary max special $max-special";
say "Primary Max bitwidth: ", uint-bitwidth($max-primary),
" Secondary Max bitwidth: ", uint-bitwidth($max-secondary),
" Tertiary Max bitwidth: ", uint-bitwidth($max-tertiary),
" Special Max bitwidth: ", uint-bitwidth($max-special);

my @names =
    'primary', 'secondary', 'tertiary', 'special';

my @list-for-packing =
    0 => uint-bitwidth($max-primary),
    1 => uint-bitwidth($max-secondary),
    2 => uint-bitwidth($max-tertiary),
    3 => uint-bitwidth($max-special);
use lib 'lib';
use BitfieldPacking; use bitfield-rows-switch;
my @order = compute-packing(@list-for-packing);
say "my order is @order.perl()";
@composed-arrays.push: 'struct collation_key {';
for @order -> $pair {
    @composed-arrays.push: ([~] "unsigned int ", @names[$pair.key], " :", $pair.value, ";").indent(4);
}
@composed-arrays.push: '};';
my @names2 =
    'codepoint', 'sub_node_elems', 'sub_node_link',
    'collation_key_elems', 'collation_key_link';
my @list-for-packing2 =
    0 => uint-bitwidth($max-cp),
    1 => uint-bitwidth(@main-node.elems - 1),
    2 =>  int-bitwidth(@main-node.elems - 1),
    3 => uint-bitwidth($max-collation-elems),
    4 => int-bitwidth(@collation-elements.elems - 1);
sub transform-array (@array, @order) {
    @array.map(-> $item {
        my @out;
        for ^$item.elems -> $i {
            @out[$i] = $item[@order[$i].key];
        }
        #say "item: ", $item, " out: ", @out;
        @out;
    });
}
#@composed-arrays.push: slurp-snippets('collation', 'head');
@composed-arrays.push: $struct;
@composed-arrays.push: "#define main_nodes_elems @main-node.elems()";
@composed-arrays.push: "#define starter_main_nodes_elems $main-node-elems";
@composed-arrays.push: compose-array('sub_node', 'main_nodes', @main-node».build);
@composed-arrays.push: "#define special_collation_keys_elems @collation-elements.elems()";
@composed-arrays.push: compose-array( 'struct collation_key', 'special_collation_keys', transform-array(@collation-elements, @order));
@composed-arrays.push: Q:to/END/;
int min (sub_node node) {
    return node.sub_node_link == -1 ? -1 : main_nodes[node.sub_node_link].codepoint;
}
int max (sub_node node) {
    return node.sub_node_link == -1 ? -1 : main_nodes[node.sub_node_link + node.sub_node_elems - 1].codepoint;
}
END
spurt "src/strings/unicode_uca.c", @composed-arrays.join("\n");
test-it("AAB5 AA87");
test-it("0F71 0F72");
