#!/usr/bin/env raku
# This script parses the expression templates and orders it based on the
# oplist order. It attaches any text between two template definitions
# (i.e. comments) with the following op so that comments are preserved.
# Any text before the first template is pinned to the top of the output
# file. Before writing over the previous expression template file it makes
# sure the output is the same number of graphemes as the input (minus
# newlines).
my Str:D $file = slurp 'src/jit/core_templates.expr';
my @spots;
my @newlines;
sub peek (Int $i) {
    $file.substr: $i, 20;
}
loop (my Int $i = 0; $i < $file.chars; $i++) {
    my $char = $file.substr: $i, 1;
    if $char eq '#' && ($i == 0 || $file.substr($i-1, 1) eq "\n") {
        #say "found # at $i";
        while $file.substr($i, 1) ne "\n" {
            $i++;
        }
        #say "found newline after # at $i";
        push @newlines, $i;
        die if $file.substr($i, 1) ne "\n";
    }
    else {
        use nqp;
        if nqp::eqat($file, "(template:", $i) {
            #say "found template start at $i";
            my @stack = $file.substr: $i, 1;
            my $start = $i;
            $i++;
            while @stack {
                my $char = $file.substr: $i, 1;
                #print $char;
                if $char eq '(' {
                    #say "open at $i", "stack is ", @stack.Int;
                    #say peek $i;
                    @stack.push: $char;
                }
                elsif $char eq ')' {
                    my $rtrn = @stack.pop;
                    die "unmatching number of () at char $i" if $rtrn ne '(';
                }
                $i++;
            }
            $i--;
            my $end = $i;
            @spots.push: [$start, $end];
        }
    }
}
my %parts;
my $last = -1;
my $prefix;
sub oplistorder {
     qx{./tools/compare-oplist-interp-order.sh --get-oplist-order}.lines
}
loop ($i = 0; $i < @spots; $i++) {
    my $prev-start = $last + 1;
    my $prev-end   = @spots[$i][0] - 1;
    my $prev = $file.substr: $prev-start, $prev-end - $prev-start +1;
    my $current = $file.substr: @spots[$i][0], @spots[$i][1] - @spots[$i][0] + 1;
    $current ~~ /'(template:' \s+ (<[_a..zA..Z0..9]>+)'!'?\s/;
    my $opname =  $0.Str;
    die "Couldn't find opname. Likely the regex needs to be adjusted: $current"
        if !$opname;
    #say $opname;
    my $total;
    if $last == -1 {
        $total = $current;
        $prefix = $prev;
    }
    else {
        $total = $prev ~ $current;
    }
    %parts{$opname} = $total;
    $last = @spots[$i][1];
}
my @out = $prefix;
for oplistorder() -> $op {
    @out.push: %parts{$op} if %parts{$op}:exists;
}
die "seems like we lost some characters not counting newlines" if @out.join.lines.join.chars != $file.lines.join.chars;
"src/jit/core_templates.expr".IO.spurt: @out.join.chomp ~ "\n"
