#| :partition-note option seperates them by line and adds comments so you can tell
#| what element number each one of the items is
#|
#| :map-empty-as allows you to make undefined array items to a certain value.
#| for example match all undefined items to -1 or 0 for example
#| function compose-array2-debug was created for debugging the creation of the arrays
#| and adding comments between each item for manual debugging
multi compose-array2-debug (
    Str:D   $type,
    Str:D   $name,
            @body,
    Bool   :$header   = False,
    Str:D  :$delim    = ',',
    Bool:D :$no-split = False,
           :$partition-note!,
           :$map-empty-as
) is export {
    @body .= map({ .defined ?? $_ !! $map-empty-as })
        if $map-empty-as;
    say "============$name==========";
    my $p-note-nl-delim = S/','/,\n/ given $partition-note;
    compose-array($type, $name, @body, :$header, :$delim, :$no-split)
        .split($partition-note)
        .map({$_ ~ "/*" ~ $++ ~ "*/" })
        .join($p-note-nl-delim)
}
multi compose-array (
    Str:D   $type,
    Str:D   $name,
    Cool:D  $elems,
    Str:D   $body,
    Bool:D :$header   = False,
    Str:D  :$delim    = ',',
    Bool:D :$no-split = False
) is export {
    if $header {
        "#define {$name}_elems $elems" ~ "\n" ~ $type ~ " $name\[" ~ $elems ~ '];';
    }
    else {
        ($type,
        " $name\[" ~ $elems ~ '] = {' ~ "\n",
        ($no-split ?? $body.join($delim) !! break-into-lines($body, $delim)),
        '};',
        "\n").join;
    }
}
multi compose-array (
    Str:D   $type,
    Str:D   $name,
            @body where { all($_ Z~~ any(Str, Int), *) },
    Bool   :$header   = False,
    Str:D  :$delim    = ',',
    Bool:D :$no-split = False,
) is export {
    #note "Composing array [$name] type: $type";
    if $type.contains('char *') {
        return compose-array($type, $name, @body.elems, '"' ~ @body.join('","') ~ '"', :header($header), :$delim, :$no-split);
    }
    elsif $type.contains('char') {
        # Use a null char to denote empty items since you can't have an empty
        # char in C
        $_ = '\0' if $_ eq '' for @body;
        return compose-array($type, $name, @body.elems, ｢'｣ ~ @body.join(｢','｣) ~ ｢'｣, :header($header), :$delim, :$no-split);
    }
    compose-array($type, $name, @body.elems, @body.join(','), :header($header), :$delim);
}
multi compose-array (
    Str:D   $type,
    Str:D   $name,
            @body where { .all ~~ Positional },
    Bool   :$header     = False,
    Str:D  :$delim      = ',',
    Bool:D :$no-quoting = False,
    Bool:D :$no-split   = False
) is export {
    compose-array($type, $name, @body.map({ '{' ~ .map({ (!$no-quoting && $_ ~~ Str) ?? “"$_"” !! $_}).join(',') ~ '}' }), :$header, :$delim, :$no-split);
}

sub break-into-lines (Str $string, Str $breakpoint) {
    my $copy = $string;
    $copy ~~ s:g/(.**70..79 $breakpoint)/$0\n/;
    return $copy;
}
