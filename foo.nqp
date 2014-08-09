#!/usr/bin/env nqp-m


sub test-it() {
    try {
        nqp::die("OH HAI");
        CATCH { nqp::say($_); }
    }
}

my int $i := 0;
while $i < 100 { 
    test-it();
    $i := $i + 1;
}