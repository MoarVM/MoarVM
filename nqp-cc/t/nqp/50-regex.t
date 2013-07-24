#! nqp

plan(7);

ok(NQPCursor.parse('a', :rule(/<alpha>/), :p(0)),
        'Can parse "a" with <alpha> and :p(0)');

ok(!NQPCursor.parse('a', :rule(/<alpha>/), :p(1)),
        'Can parse "a" with <alpha> :p(off-range)');

ok(!NQPCursor.parse('a', :rule(/<alpha>/), :c(1)),
        'Can parse "a" with <alpha> :c(off-range)');

ok(!NQPCursor.parse('a', :rule(/<alpha>/), :p(5)),
        'Can parse "a" with <alpha> :p(far-off-range)');

ok(?('ABC' ~~ /:i abc/), ':i works with literals');
ok(?('ABC' ~~ /:i 'abc'/), ':i works with single-quoted literals');
ok(?('ABC' ~~ /:i "abc"/), ':i works with double-quoted literals');
