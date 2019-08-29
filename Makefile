.PHONY: all, clean

all: virt pith

virt: virt.c
	gcc -o virt virt.c

pith: main.o asm.o linker.ld
	# gcc -m64 -fno-pie -ffreestanding -nostdlib -o pith.o main.o asm.o #for debug
	gcc -m64 -fno-pie -ffreestanding -nostdlib -T linker.ld -o pith main.o asm.o

main.o: main.c
	gcc -m64 -fno-pie -ffreestanding -nostdlib -c main.c -o main.o

asm.o: asm.S
	gcc -m64 -fno-pie -ffreestanding -nostdlib -c asm.S -o asm.o

clean:
	rm *.o pith
