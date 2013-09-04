plan(4);

{
    my int $x;
    ok(~$x eq '0', 'native int initialized to 0');
    
    $x := 42;
    ok($x == 42, 'can set native int');
    
    sub foo() {
        my $y := 58;
        ok($x == 42, 'can access native ints from nested scopes');
        ok($x + $y == 100, 'can add native ints to each other');
    }
    foo();
}
