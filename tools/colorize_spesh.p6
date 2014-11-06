sub colorblock($num, $ver) {
    "\e[48;5;{ 0x10 + (($num + 1) * 31416) % 216 }m  "
    ~ "\e[38;5;{ 0xE8 + 24 - ($num + $ver * 5) % 24 }m\c[ BLACK LEFT-POINTING TRIANGLE ]"
    ~ "\e[48;5;0m\c[ BLACK RIGHT-POINTING TRIANGLE ]"
    ~ "\e[m"
}

for lines() :eager -> $_ is copy {
    .subst( /r(<.digit>+) '(' (<.digit>+) ')' /, -> $/ { "r$0\($1\)" ~ colorblock($0, $1) }, :g ).say
}
