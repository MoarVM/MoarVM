my $interp_c_file = $*PROGRAM-NAME.IO.parent.parent.child('src').child('core').child('interp.c');

my $cur_op = 'before_dispatch';

my %lines_to_op;

for $interp_c_file.lines.kv -> $lineno, $line {
    if $line ~~ / ^ \s* 'OP(' $<opname>=<[a..z A..Z \- \_ 0..9]>+ '):' / {
        $cur_op = $<opname>;
    }

    elsif $line ~~ / ^ \s* 'default:' / {
        $cur_op ~= "_or_after_dispatch";
    }

    %lines_to_op{$lineno} = $cur_op;
}

for lines() -> $_ is copy {
    $_ .= subst(rx/'interp.c:' $<lineno>=<[0..9]>+ /,
            -> $/ { "interp.c:{$<lineno>} ({%lines_to_op{$<lineno>}})" },
            :g);
    .say;
}
