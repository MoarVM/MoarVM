#! nqp

plan(20);

## open, printfh, readallfh, closefh
my $test-file := 'test-nqp-73';
nqp::unlink($test-file) if nqp::stat($test-file, 0); # XXX let mvm die on nonexistent file

my $fh := nqp::open($test-file, 'w');
ok($fh, 'we can open a nonexisting file for writing');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'w');
ok($fh, 'we can open an existing file for writing');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'r');
ok(nqp::readallfh($fh) eq '', 'test file is empty');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'wa');
ok(nqp::printfh($fh, "awesome") == 7, 'appended a string to that file');
ok(nqp::printfh($fh, " thing!\n") == 8, 'appended a string to that file... again');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'r');
ok(nqp::readallfh($fh) eq "awesome thing!\n", 'test file contains the strings');
ok(nqp::tellfh($fh) == 15, 'tellfh gives correct position');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'w');
nqp::closefh($fh);
$fh := nqp::open($test-file, 'r');
ok(nqp::readallfh($fh) eq '', 'opening for writing truncates the file');
nqp::closefh($fh);

## setencoding
$fh := nqp::open($test-file, 'w');
nqp::setencoding($fh, 'utf8');
ok(nqp::printfh($fh, "ä") == 2, 'umlauts are printed as two bytes');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'r');
nqp::setencoding($fh, 'utf8'); # XXX let ascii be the default
my $str := nqp::readallfh($fh);
ok(nqp::chars($str) == 1, 'utf8 means one char for an umlaut');
ok($str eq "ä", 'utf8 reads the umlaut correct');
nqp::closefh($fh);

$fh := nqp::open($test-file, 'r');
nqp::setencoding($fh, 'iso-8859-1');
ok(nqp::chars(nqp::readallfh($fh)) == 2, 'switching to ansi results in 2 chars for an umlaut');
nqp::closefh($fh);

## chdir
nqp::chdir('t');
$fh := nqp::open('../' ~ $test-file, 'r');
nqp::setencoding($fh, 'utf8');
ok(nqp::chars(nqp::readallfh($fh)) == 1, 'we can chdir into a subdir');
nqp::closefh($fh);

nqp::chdir('..');
$fh := nqp::open($test-file, 'r');
nqp::setencoding($fh, 'utf8');
ok(nqp::chars(nqp::readallfh($fh)) == 1, 'we can chdir back to the parent dir');
nqp::closefh($fh);

## mkdir
nqp::mkdir($test-file ~ '-dir', 0o777);
nqp::chdir($test-file ~ '-dir');
$fh := nqp::open('../' ~ $test-file, 'r');
nqp::setencoding($fh, 'utf8');
ok(nqp::chars(nqp::readallfh($fh)) == 1, 'we can create a new directory');
nqp::closefh($fh);
nqp::chdir('..');

nqp::rmdir($test-file ~ '-dir');
nqp::unlink($test-file);

$fh := nqp::open('t/nqp/77-readline.txt', 'r');
ok(nqp::readlinefh($fh) eq 'line1', 'reading a line till CR');
ok(nqp::readlinefh($fh) eq 'line2', 'reading a line till CRLF');
ok(nqp::readlinefh($fh) eq 'line3', 'reading a line till LF');
ok(nqp::readlinefh($fh) eq '',      'reading an empty line');
ok(nqp::readlinefh($fh) eq 'line4', 'reading a line till EOF');
nqp::closefh($fh);


# vim: ft=perl6

