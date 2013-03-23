#! nqp

# check control structure 'if ... else'

say('1..14');

if 1 { say("ok 1 # on one line with else"); } else { say("not ok 1 # on one line with else")}

say("ok 2 # statements following if with else are okay");

if 1 {
    print("ok 3");
}
else {
    print("not ok 3");
}
say(" # multi-line if with else");

if 0 {
    print("not ok 4");
}
else {
    print("ok 4");
}
say(" # multi-line if, else branch");

if 0 {
}
else {
    print("ok 5");
}
say(" # empty if-block");

if 0 {
    print("not ok 6");
}
else {
}
print("ok 6");
say(" # empty else-block");

if 0 {
}
else {
}
print("ok 7");
say(" # empty if- and else-block");

if 0 {
}
elsif 0 {
}
elsif 0 {
}
else {
}
print("ok 8");
say(" # empty if-, elsif-, and else-block");

if 1 {
    print("ok 9");
}
elsif 0 {
    print("not ok 9 # elsif 1");
}
elsif 0 {
    print("not ok 9 # elsif 2");
}
else {
    print("not ok 9 # else");
}
say(" # if expr true in if/elsif/elsif/else");

if 0 {
    print("not ok 10 # if");
}
elsif 1 {
    print("ok 10");
}
elsif 0 {
    print("not ok 10 # elsif 2");
}
else {
    print("not ok 10 # else");
}
say(" # first elsif expr true in if/elsif/elsif/else");

if 0 {
    print("not ok 11 # if");
}
elsif 0 {
    print("not ok 11 # elsif 1");
}
elsif 1 {
    print("ok 11");
}
else {
    print("not ok 11 # else");
}
say(" # second elsif expr true in if/elsif/elsif/else");

if 0 {
    print("not ok 12 # if");
}
elsif 1 {
    print("ok 12");
}
elsif 1 {
    print("not ok 12 # elsif 2");
}
else {
    print("not ok 12 # else");
}
say(" # first and second elsif expr true in if/elsif/elsif/else");

if 0 {
    print("not ok 13 # if");
}
elsif 0 {
    print("not ok 13 # elsif 1");
}
elsif 0 {
    print("not ok 13 # elsif 2");
}
else {
    print("ok 13");
}
say(" # else expr true in if/elsif/elsif/else");

if 0 { } elsif 0 { }
print("ok 14");
say(" # no else block in if/elsif")
