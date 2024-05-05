TEST_MODE = random

test: test_ clean

test_: main vhd
	./main $(TEST_MODE) vhd

gdb: main vhd
	lldb ./main interactive vhd

vhd:
	dd if=/dev/zero of=vhd bs=512 count=1024

main: *.c
	gcc $^ -g -o $@

clean:
	rm -rf vhd main log
