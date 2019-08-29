#include <stdint.h>

struct idt_desc {
    uint16_t offset_1; // offset bits 0..15
    uint16_t selector; // a code segment selector in GDT or LDT
    uint8_t ist;       // bits 0..2 holds Interrupt Stack Table offset, rest of bits zero.
    uint8_t type_attr; // type and attributes
    uint16_t offset_2; // offset bits 16..31
    uint32_t offset_3; // offset bits 32..63
    uint32_t zero;     // reserved
} __attribute__((packed));

static inline void load_idt (struct idt_desc *idt)
{
    asm volatile ("lidt %0" :: "m"(*idt));
}

static inline void
outb (unsigned char value, unsigned short int port)
{
  asm volatile ("outb %b0,%w1": :"a" (value), "Nd" (port));
}

static inline void wrmsr(uint64_t msr, uint64_t value)
{
	uint32_t low = value & 0xFFFFFFFF;
	uint32_t high = value >> 32;
	asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint64_t msr)
{
	uint32_t low, high;
	asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
	return ((uint64_t)high << 32) | low;
}

void syscall_handler ()
{
    asm volatile ("outb %b0,%w1": :"a" (6), "Nd" (6));
    outb (7, 7);
    asm volatile ("hlt");
}

extern void _syscall ();

#define IA32_STAR  0xC0000081
#define IA32_LSTAR 0xC0000082

int kernel_main ()
{
    /* for (int i=0; i<255; i++) */
    /* { */
    /*     outb (i, 0); */
    /* } */
    uint64_t r = (uint64_t)&_syscall;
    for (int i=0; i<8; i++)
    {
        outb (r & 0xff, 0);
        r >>= 8;
    }
   
    wrmsr (IA32_LSTAR, (uint64_t)&_syscall);

    r = rdmsr (IA32_LSTAR);
    for (int i=0; i<8; i++)
    {
        outb (r & 0xff, 0);
        r >>= 8;
    }

    outb (77, 0);

    r = rdmsr (IA32_STAR);
    for (int i=0; i<8; i++)
    {
        outb (r & 0xff, 0);
        r >>= 8;
    }


    asm volatile ("syscall");
}

__attribute__((section(".start")))
void _start ()
{
    kernel_main ();

    asm volatile ("hlt");

    while (1);
}
