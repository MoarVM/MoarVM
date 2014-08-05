#!/usr/bin/env nqp-m

class Foo {
has @!foo;
proto method wam($f) {*}
multi method wam(Foo $f) {
    nqp::say("OH HAI");
}
}

grammar Bar {
    token integer { \d+ }
    token sign    { <[+-]> }
    token signed_integer { <sign>? <integer> }
    token key { \w+ }
    token value { \N+ }
    token entry { <key> \h* '=' \h* <value> }
    token entries { [ <entry> \n | \n ]+ }
    token section { '[' ~ ']' <key> \n <entries> }
    token TOP {
        ^
            <entries>
            <section>+
        $
    }
}

class BarActions {
    method entries($/) {
        my %entries;
        for $<entry> -> $e {
            %entries{$e<key>} := ~$e<value>;
        }
        make %entries;
    }
    
    method TOP($/) {
        my %result;
        %result<_> := $<entries>.ast;
        for $<section> -> $sec {
            %result{$sec<key>} := $sec<entries>.ast;
        }
        make %result;
    }
}

my str $data := Q{
name = Rat Facts
author = brrt

[neno]
evil = true
fast = true
color = ninja

[noppes]
evil = false
fast = false
color = grey
};

my int $i := 0;
while $i < 100 {
    my $m := Bar.parse($data, :actions(BarActions));
    my %sections := $m.ast;
    for %sections -> $sec {
        nqp::say("Section {$sec.key}");
        for $sec.value -> $entry { 
            nqp::say("    {$entry.key}: {$entry.value}");
        }
    }
    $i := $i + 1;
}