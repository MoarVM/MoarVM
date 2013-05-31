#! nqp

# test optional arguments and parameters

plan(3);

sub f1 ($x, $y!, $z?) {
  $x;
}
say('ok ', f1(1, 2), ' # optional args ignorable');
say('ok ', f1(2, 2, 2), ' # optional args passable');

sub f2 ($x?, $y?) { 'ok 3 # only optional args'; }
say(f2());

# TODO we can't parse .defined() yet - jg
#sub f3 ($x, $y?, $text?) {
#  if ! $y.defined() && ! $text.defined() {
#    say('ok 4 # unpassed optional args are undef');
#  } else {
#    say('ok ', $x - $y, $text);
#  }
#}
#f3(2);
#f3(8, 3, ' # optional args get passed values');
#f3(8, :text(' # optional args specifiable by name'), :y(2));

# XXX: need to be able to test that the following is illegal
#sub f4 ($x?, $y) { $y; }

