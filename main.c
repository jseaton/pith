#include <stdint.h>

static inline void
outb (unsigned char value, unsigned short int port)
{
  asm volatile ("outb %b0,%w1": :"a" (value), "Nd" (port));
}

int kernel_main ()
{
    for (int i=0; i<255; i++)
    {
        asm volatile ("outb %b0,%w1": :"a" (3), "Nd" (0));
        outb (4, 0);
    }
}

__attribute__((section(".start")))
void _start ()
{
    kernel_main ();

    asm volatile ("hlt");

    while (1);
}
