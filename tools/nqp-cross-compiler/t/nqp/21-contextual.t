#! nqp

# Tests for contextual variables

plan(7);

sub foo() { $*VAR }

{
    my $*VAR := 'abc';
    ok($*VAR eq 'abc', 'basic contextual declaration works');
    ok(foo() eq 'abc', 'called subroutine sees caller $*VAR');

    sub bar() { $*VAR }

    ok(bar() eq 'abc', 'called subroutine sees caller $*VAR');



    {
        my $*VAR := 'def';
        ok( $*VAR eq 'def', 'basic nested contextual works');
        ok( foo() eq 'def', 'called subroutine sees caller $*VAR');
        ok( bar() eq 'def', 'called subroutine sees caller not outer');
    }

    ok($*VAR eq 'abc', 'nested contextuals don\'t affect outer ones');
}


