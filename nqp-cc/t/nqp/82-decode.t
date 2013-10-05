plan(3);

class int8 is repr('P6int') {}
class buf8 is repr('VMArray') {}

nqp::composetype(int8, nqp::hash('integer', nqp::hash('bits', 8)));
nqp::composetype(buf8, nqp::hash('array', nqp::hash('type', int8)));

class int16 is repr('P6int') {}
class buf16 is repr('VMArray') {}

nqp::composetype(int16, nqp::hash('integer', nqp::hash('bits', 16)));
nqp::composetype(buf16, nqp::hash('array', nqp::hash('type', int16)));

my $buf := nqp::encode('', 'utf8', buf8.new);
ok(1, 'encoding empty string as UTF-8');
my $str := nqp::decode($buf, 'utf8');
ok(1, 'decoding empty UTF-8 string into string');

# Test for a regression in the Parrot backend.
$buf := nqp::encode('a', 'utf16', buf16.new);
$str := nqp::decode($buf, 'utf16');
ok(1, 'round-tripping via UTF-16');
