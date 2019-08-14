.PHONY: all, clean

all: virt vos

virt: virt.c
	gcc -o virt virt.c

vos: main.o linker.ld
	gcc -m64 -fno-pie -ffreestanding -nostdlib -m32 -T linker.ld -o vos main.o

main.o: main.c
	gcc -m64 -fno-pie -ffreestanding -nostdlib -m32 -c main.c -o main.o

clean:
	rm *.o vos
