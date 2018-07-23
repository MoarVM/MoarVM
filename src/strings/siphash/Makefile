all:
	echo "Testing unaligned reads" && gcc -O3 test.c -g -Wall -Wextra -ggdb -o siphashtest-unaligned -D MVM_CAN_UNALIGNED_INT64 -D MVM_CAN_UNALIGNED_INT32
	echo "Testing only aligned reads" && gcc -O3 test.c -g -Wall -Wextra -ggdb -o siphashtest-aligned
test:
	./siphashtest-unaligned
	./siphashtest-aligned
