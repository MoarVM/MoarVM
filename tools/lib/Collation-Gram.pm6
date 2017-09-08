grammar Collation-Gram {
    token TOP {
        <codepoints>
        \s* ';' \s*
        <coll-key>+
        <comment>
        .*
    }
    token codepoints {
        <codepoint>+ % \s+
    }
    token codepoint {
        <:AHex>+
        #[$<cp>=(<:AHex>)\s+]
    }
    token comment { \s* '#' \s* <( .* $ }
    token coll-key {
        '[' ~ ']'
        [
            <dot-star> <primary> '.' <secondary> '.' <tertiary>
        ]
    }
    token dot-star { <[.*]> }
    token primary { <:AHex>+ }
    token secondary { <:AHex>+ }
    token tertiary { <:AHex>+ }
}
class Collation-Gram::Action {
    has @!array;
    has $!comment;
    has $!dot-star;
    has @!codepoints;
    method TOP ($/) {
        @!codepoints = @!codepoints.chrs.ords;
        if $<comment> eq 'GREEK NUMERAL SIGN' {
            say $/ ~ "\n" ~ @!codepoints;
        }
        make %(
            array => @!array,
            comment => ~$<comment>,
            codepoints => @!codepoints.chrs.ords
        )
    }
    method coll-key ($/) {
        my $a = ($<primary>, $<secondary>, $<tertiary>).map(*.Str.parse-base(16)).Array;
        $a.push: ($<dot-star> eq '.' ?? 0 !! $<dot-star> eq '*' ?? 1 !! do { die $<dot-star> });
        @!array.push: $a;
    }
    method codepoints ($/) {
        @!codepoints.append: $<codepoint>.map(*.Str.parse-base(16));
    }
}
