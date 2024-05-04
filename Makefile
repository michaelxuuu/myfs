run: testprog vhd random.txt
	./testprog vhd
	diff random.txt random1.txt

dbg: testprog vhd random.txt
	lldb ./testprog vhd

random.txt:
	python3 rand.py

vhd:
	dd if=/dev/zero of=vhd bs=512 count=1024

testprog: *.c
	gcc $^ -g -o $@

clean:
	rm -rf vhd testprog *.txt
