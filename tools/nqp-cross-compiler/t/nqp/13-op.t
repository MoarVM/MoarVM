#!./parrot nqp.pbc

# checking basic operands and circumfix:( )

plan(32);

##Additive operators
ok(      1+2  == 3, 'Checking addition 1+2');
ok(     10-9  == 1, 'Checking subtraction 10-9');
ok(   10-3+2  == 9, 'Checking compound statements 10-3+2');
ok(  10-(3+2) == 5, 'Checking parenthesized statement 10-(3+2)');

##Multiplicative operators
ok(      6*7  == 42, 'Checking multiplication 6*7');
ok(     36/6  ==  6, 'Checking division 36/6');
ok(    4*3+5  == 17, 'Checking compound statements 4*3+5');
ok(   4*(3+5) == 32, 'Checking parenthesized statements 4*(3+5)');
ok(   12/4*3  ==  9, 'Checking compound statements 12/4*3');
ok( 12/(4*3)  ==  1, 'Checking compound statements 12/(4*3)');
ok(   5-3*2   == -1, 'Checking compound statements 5-3*2');

##Modulo operator
ok(      8%3  == 2, 'Checking modulo 8%3');
ok(    8%3+2  == 4, 'Checking compound statement 8%3+2');
ok(  8%(3+2)  == 3, 'Checking compound statement 8%(3+2)');

##Concatenation operator
ok( 'a' ~ 'b' eq 'ab', 'Checking concatenation "a" ~ "b"');
ok(  1  ~ 'b' eq '1b', 'Checking concatenation  1  ~ "b"');
ok( 'a' ~  2  eq 'a2', 'Checking concatenation "a" ~  2 ');

##Postfix operators
my $x := 0;
ok( $x++ == 0 );
ok( $x   == 1 );
ok( $x-- == 1 );
ok( $x   == 0 );

##Relational operators
ok( ?(1 <  2) );
ok( !(2 <  1) );
ok( ?(2 <= 2) );
ok( !(3 <= 2) );
ok( ?(2 >  1) );
ok( !(2 >  3) );
ok( ?(2 >= 1) );
ok( !(2 >= 3) );

#Bitwise operators
ok( (1 +| 3) == 3, 'Checking 1 +| 3' );
ok( (3 +& 2) == 2, 'Checking 3 +& 2' );
ok( (3 +^ 3) == 0, 'Checking 3 +^ 3' );

