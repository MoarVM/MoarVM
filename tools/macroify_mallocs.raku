my $text = slurp();

my $comments = $text.match(:g, / '/*' .*? '*/' /);

my %alloctypemap = <
    m MVM_malloc_array
    c MVM_calloc_array
>;

my @substitutions;

for $text.split(";") {
    my $line = $_.trim-leading;
    sub skip-because($reason) {
        #note "skipped command $line.substr(0, 3) because $reason";
        next;
    }

    next unless $line.chars;
    next if $line eq '}';

    skip-because("preprocessor stuff") if .starts-with("#");

    skip-because("no MVM and no alloc") unless .contains("alloc") && .contains("MVM_");

    skip-because("no MVM_.?alloc") unless / MVM_[m|c|re|rec]alloc /;

    skip-because("array of pointers") if .contains('**)') || .contains('** )');

    my regex formula {
        | <ident>? '(' <-[\)]>+ ')'
        | \S+
    }

    #/ :s <?> \( $<type>=[<ident>] '*' \) (MVM_[m|c]alloc) \( <formula> '*' 'sizeof' '(' $<type>=[ 'MVM' <-[\s]>+ & <ident> ] ')' /;
    / [\( $<casttype>=[<ident>] \s* '*' \) \s*]? (MVM_(m|c)alloc) \s* \( [<formula> \s* ['*' | ',']]? \s* 'sizeof' \s* \( \s* $<type>=<ident> \s* \) \) /;

    unless $/ {
        say "REJECTED";
        .say;
    }

    next unless $/;
    #.say;

    say "original: $/.Str()";

    my $formula = $<formula>.Str;
    $formula .= substr(1, *-1) if $formula.chars && $formula.starts-with("(") and $formula.ends-with(")");

    $formula ~= ", " if $formula.chars;

    $formula ||= "1, ";

    say "";
    say "      ", my $result = "%alloctypemap{$0[0]}\($formula$<type>\)";
    say "";

    @substitutions.push([$/.Str, $result]);
}

for @substitutions {
    $text .= subst(.[0], .[1]);
}

spurt(@*ARGS[0], $text);
