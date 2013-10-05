plan(9*3);
sub test_radix($radix,$str,$pos,$flags,$value,$mult,$offset,$desc) {
    my $result := nqp::radix($radix,$str,$pos,$flags);
    ok($result[0] == $value,"$desc - correct converted value");
    ok($result[1] == $mult,"$desc - correct radix ** (number of digits converted)");
    ok($result[2] == $offset,"$desc - correct offset");
}
test_radix(10,"123",0,2,  123,1000,3,  "basic base-10 radix call");
test_radix(10,"123",1,2,  23,100,3, "basic base-10 radix call with pos" );
test_radix(2,"100",0,2,  4,8,3, "basic base-2 radix call" );
test_radix(15,"1A",0,2,  25,225,2, "base 15 call with lower case" );
test_radix(15,"1B",0,2,  26,225,2, "base 15 call with upper case" );
test_radix(15,"-1B",0,2,  -26,225,3, "base 15 call with upper case and negation" );
test_radix(10,"000123",0,2,  123,1000000,6,  "base-10 with zeros at the front");
test_radix(10,"1_2_3",0,2,  123,1000,5,  "base-10 with underscores");
test_radix(10,"not_a_number",0,2,  0,1,-1,  "no digits consumed");
