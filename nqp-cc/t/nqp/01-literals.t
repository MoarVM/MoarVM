#! nqp

# check literals

say('1..9');

print("ok ");
print(1);
print("\n");

print('ok ');
say(2);

print("ok 3\n");
say('ok 4');
say("ok 5");
say(q<ok 6>);
say(q  <ok 7>);

say("\x6f\x6b 8");

print("ok ");
say(0_9);
