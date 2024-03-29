#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/kvm.h>
#include <asm/processor-flags.h>

#define EFER_SCE 1
#define EFER_LME (1U << 8)
#define EFER_LMA (1U << 10)
#define EFER_NXE (1U << 11)

/* 64-bit page * entry bits */
#define PDE64_PRESENT 1
#define PDE64_RW (1U << 1)
#define PDE64_USER (1U << 2)
#define PDE64_ACCESSED (1U << 5)
#define PDE64_DIRTY (1U << 6)
#define PDE64_PS (1U << 7)
#define PDE64_G (1U << 8)

// TODO all of these should be shared with the linker script somehow
#define KERN_CODE_SIZE   0x10000
#define KERN_CODE_START  0x100000

#define KERN_DATA_SIZE   0x10000
#define KERN_DATA_START  0x200000
#define KERN_PML4_START  (KERN_DATA_START + KERN_DATA_SIZE - 0x1000)
#define KERN_PDPT_START  (KERN_PML4_START - 0x1000)
#define KERN_PD_START    (KERN_PDPT_START - 0x1000)
#define KERN_GDT_SIZE    0x1000
#define KERN_GDT_START   (KERN_PD_START   - KERN_GDT_SIZE)
#define KERN_TSS_START   (KERN_PD_START   - 0x1000)
#define KERN_STACK_START (KERN_GDT_START  - 0x2000)

#define USER_MEM_SIZE  0x10000
#define USER_MEM_START 0x0


struct gdt_desc {
	uint16_t limit0;
	uint16_t base0;
	unsigned base1:8, s:1, type:4, dpl:2, p:1;
	unsigned limit1:4, avl:1, l:1, db:1, g:1, base2:8;
	uint32_t base3;
	uint32_t zero1;
} __attribute__((packed));

void create_gdt_desc (uint8_t *kern_mem, struct kvm_segment *seg)
{
	struct gdt_desc *desc = (struct gdt_desc *)(kern_mem + KERN_DATA_START - KERN_GDT_START + (seg->selector >> 3) * 8);

	desc->limit0 = seg->limit & 0xFFFF;
	desc->base0  = seg->base & 0xFFFF;
	desc->base1  = seg->base >> 16;
	desc->s      = seg->s;
	desc->type   = seg->type;
	desc->dpl    = seg->dpl;
	desc->p      = seg->present;
	desc->limit1 = seg->limit >> 16;
	desc->l      = seg->l;
	desc->db     = seg->db;
	desc->g      = seg->g;
	desc->base2  = seg->base >> 24;

	if (!seg->s)
		desc->base3 = seg->base >> 32;
}

void handleHypercall (struct kvm_run *run)
{
    if (run->io.port < 2) // I/O
    {
        if (run->io.direction == KVM_EXIT_IO_IN) // Input
            fprintf (stderr, "INPUT\n");
        else
        {
            FILE *output = run->io.port == 0 ? stdout : stderr;
            fprintf (output, "OUTPUT '");
            fwrite  (((uint8_t *)run) + run->io.data_offset, 1, run->io.size, output);
            fprintf (output, "' (%d)\n", *(((uint8_t *)run) + run->io.data_offset));
        }
    }
}

int main (int argc, char **argv)
{
    int fd = open(argv[1], O_RDONLY, 0);
    assert (fd != -1);

    struct stat st;
    stat(argv[1], &st);
    size_t code_size = st.st_size;
    assert (code_size < KERN_CODE_SIZE);
    uint8_t *code_mem = mmap(NULL, code_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);

    int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    assert (kvm != -1);

    int ret = ioctl(kvm, KVM_GET_API_VERSION, NULL);
    assert (ret == 12);

    int vmfd = ioctl(kvm, KVM_CREATE_VM, (unsigned long)0);
    assert (vmfd != -1);

    uint8_t *kern_mem = mmap(NULL, KERN_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert (kern_mem);

    uint8_t *user_mem = mmap(NULL, USER_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert (user_mem);

#define PRINT_STACK() \
    for (int i=0; i<100; i++) \
        printf ("%02x ", *(kern_mem + KERN_STACK_START - KERN_DATA_START + i - 100)); \
    printf ("\n");

	int slots = ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_NR_MEMSLOTS);
    assert (slots > 2);

    struct kvm_userspace_memory_region kernel_code_region = {
        .slot = 0,
        .flags = 0,
        .guest_phys_addr = KERN_CODE_START,
        .memory_size = KERN_CODE_SIZE,
        .userspace_addr = (uint64_t)code_mem,
    };

    struct kvm_userspace_memory_region kernel_data_region = {
        .slot = 1,
        .flags = 0,
        .guest_phys_addr = KERN_DATA_START,
        .memory_size = KERN_DATA_SIZE,
        .userspace_addr = (uint64_t)kern_mem,
    };

    struct kvm_userspace_memory_region user_region = {
        .slot = 2,
        .flags = 0,
        .guest_phys_addr = USER_MEM_START,
        .memory_size = USER_MEM_SIZE,
        .userspace_addr = (uint64_t)user_mem,
    };

    assert (!ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &kernel_code_region));
    assert (!ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &kernel_data_region));
    assert (!ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &user_region));

    int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, (unsigned long)0);
    assert (vcpufd != -1);

    int mmap_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, NULL);
    assert (mmap_size >= sizeof(struct kvm_run));

    struct kvm_run *run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);

    struct kvm_sregs sregs;

    ioctl(vcpufd, KVM_GET_SREGS, &sregs);

    uint64_t pml4_addr = KERN_PML4_START; // Top level page table
	uint64_t *pml4 = (void *)(kern_mem - KERN_DATA_START + pml4_addr);

	uint64_t pdpt_addr = KERN_PDPT_START; // Level 3
	uint64_t *pdpt = (void *)(kern_mem - KERN_DATA_START + pdpt_addr);

	uint64_t pd_addr = KERN_PD_START; // Level 2
	uint64_t *pd = (void *)(kern_mem - KERN_DATA_START + pd_addr);

    pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;
	pd[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
	pd[1] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;

    sregs.cr3 = pml4_addr;
	sregs.cr4 = X86_CR4_PAE; // Physical Address Extensions
	sregs.cr0 = X86_CR0_PE   // protection
              | X86_CR0_MP   // monitor coprocessor
              | X86_CR0_ET   // extension type
              | X86_CR0_NE   // numeric error
              | X86_CR0_WP   // write protect
              | X86_CR0_AM   // alignment mask
              | X86_CR0_PG;  // paging
	sregs.efer = EFER_LME    // long mode enable
               | EFER_LMA;   // long mode active

    // Need this to indicate everything is long mode
    struct kvm_segment seg = {
		.base =  0,
		.limit = 0xffffffff,
		.selector = 1 << 3,
		.present = 1,
		.type = 11, /* Code: execute, read, accessed */
		.dpl = 0,
		.db = 0,
		.s = 1, /* Code/data */
		.l = 1,
		.g = 1, /* 4KB granularity */
	};

	sregs.cs = seg;

    create_gdt_desc (kern_mem, &seg);

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2 << 3;
	sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = seg;

    create_gdt_desc (kern_mem, &seg);

    struct kvm_segment tss_seg = {
		.base =  KERN_TSS_START,
		.limit = 0x67,
		.selector = 0x18,
		.present = 1,
		.type = 11, /* execute, read, accessed */
		.dpl = 0,
		.db = 0,
		.s = 0,
		.l = 0,
		.g = 0,
	};

    sregs.tr = tss_seg;

    create_gdt_desc (kern_mem, &tss_seg);
    

    sregs.gdt.base  = KERN_GDT_START;
    sregs.gdt.limit = KERN_GDT_SIZE;

    ioctl(vcpufd, KVM_SET_SREGS, &sregs);

    struct kvm_regs regs = {
        .rip = KERN_CODE_START,
        .rflags = 0x2,
        .rsp = KERN_STACK_START
    };
    ioctl(vcpufd, KVM_SET_REGS, &regs);

#define PRINT_REGS() \
    ioctl(vcpufd, KVM_GET_REGS, &regs); \
    printf ("Regs: rsp:%llx rbp:%llx rip:%llx\n", regs.rsp, regs.rbp, regs.rip);

    while (1) {
        assert (ioctl(vcpufd, KVM_RUN, NULL) != -1);
        switch (run->exit_reason) {
            	case KVM_EXIT_UNKNOWN:
                    printf ("KVM_EXIT_UNKNOWN\n");
                    break;
            	case KVM_EXIT_IO:
                    /* printf ("KVM_EXIT_IO d:%d s:%d p:%d c:%d o:%lld\n", run->io.direction, run->io.size, run->io.port, run->io.count, run->io.data_offset); */
                    handleHypercall (run);
                    break;
            	case KVM_EXIT_DEBUG:
                    printf ("KVM_EXIT_DEBUG\n");
                    break;
            	case KVM_EXIT_HLT:
                    printf ("KVM_EXIT_HLT\n");
                    return 0;
            	case KVM_EXIT_MMIO:
                    printf ("KVM_EXIT_MMIO addr:%llx data:%x write:%d\n", run->mmio.phys_addr, run->mmio.data[0], run->mmio.is_write);
                    break;
            	case KVM_EXIT_IRQ_WINDOW_OPEN:
                    printf ("KVM_EXIT_IRQ_WINDOW_OPEN\n");
                    break;
            	case KVM_EXIT_SHUTDOWN:
                    printf ("KVM_EXIT_SHUTDOWN\n");
                    break;
            	case KVM_EXIT_FAIL_ENTRY:
                    errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx",
                            (unsigned long long)run->fail_entry.hardware_entry_failure_reason);
            	case KVM_EXIT_INTR:
                    printf ("KVM_EXIT_INTR\n");
                    break;
            	case KVM_EXIT_SET_TPR:
                    printf ("KVM_EXIT_SET_TPR\n");
                    break;
            	case KVM_EXIT_TPR_ACCESS:
                    printf ("KVM_EXIT_TPR_ACCESS\n");
                    break;
            	case KVM_EXIT_NMI:
                    printf ("KVM_EXIT_NMI\n");
                    break;
                case KVM_EXIT_INTERNAL_ERROR:
                    errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x", run->internal.suberror);
                    break;
            	case KVM_EXIT_OSI:
                    printf ("KVM_EXIT_OSI\n");
                    break;
            	case KVM_EXIT_PAPR_HCALL:
                    printf ("KVM_EXIT_PAPR_HCALL\n");
                    break;
            	case KVM_EXIT_WATCHDOG:
                    printf ("KVM_EXIT_WATCHDOG\n");
                    break;
            	case KVM_EXIT_EPR:
                    printf ("KVM_EXIT_EPR\n");
                    break;
            	case KVM_EXIT_SYSTEM_EVENT:
                    printf ("KVM_EXIT_SYSTEM_EVENT\n");
                    break;
            	case KVM_EXIT_IOAPIC_EOI:
                    printf ("KVM_EXIT_IOAPIC_EOI\n");
                    break;
            	case KVM_EXIT_HYPERV:
                    printf ("KVM_EXIT_HYPERV\n");
                    break;
                default:
                    printf ("EXIT %d\n", run->exit_reason);
        }
    }
}
