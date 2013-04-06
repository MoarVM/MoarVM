#! nqp

# check positional subscripting

plan(7);

my @l := (1,2,3,4,5);

say("ok 1 # list assignment didn't barf");
say('ok ',@l[1], ' # numeric subscript');
say('ok ', @l['2'], ' # string subscript');

my $idx := 3;

say('ok ', @l[$idx], ' # variable subscript');
say('ok ', @l[$idx + 1], ' # expression in subscript');

@l[0] := 'ok 6 # string array element';
say(@l[0]);

@l[10] := 'ok 7 # automatic expansion';
say(@l[10]);

