TMODE = auto
RED=\033[1;31m
GREEN=\033[1;32m
RESET=\033[0m

.PHONY: compare

test: test_ clean

ifeq ($(TMODE), auto)
test_: main vhd
	./autotest.sh
else
test_: main vhd
	./main vhd
endif

gdb: main vhd
	lldb ./main vhd

vhd:
	dd if=/dev/zero of=vhd bs=512 count=1024

main: *.c
	gcc $^ -g -o $@

clean:
	rm -rf vhd main log *.txt
