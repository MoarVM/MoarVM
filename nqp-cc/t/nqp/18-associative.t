#!./parrot nqp.pbc

# check hash access methods

plan(7);

my %h;

%h<a> := 1;
say("ok 1 # hash assignment with numeric value works");

say('ok ', %h<a> + 1, ' # hash access to numeric value');

%h<a> := 'ok 3';
say(%h<a>, ' # hash access to string value');

%h{1} := '4';
say('ok ', %h{1}, ' # numeric hash access');

say('ok ', %h<1> + 1, ' # numbers stringify');

%h{'b'} := 'ok 6 # curly braces and single quotes work';
say(%h{'b'});

%h{"foo"} := "ok 7 # curly braces and double quotes work";
say(%h{"foo"});

