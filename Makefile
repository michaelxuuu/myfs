run: testprog vhd
	./testprog vhd

dbg: testprog vhd
	lldb ./testprog vhd

vhd:
	dd if=/dev/zero of=vhd bs=512 count=1024

testprog: *.c
	gcc $^ -g -o $@

clean:
	rm -rf testprog* vhd 
