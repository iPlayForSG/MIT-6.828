#include <kern/e1000.h>
#include <kern/pmap.h>

// LAB 6: Your driver code here

volatile uint32_t *e1000;

int
e1000_attach(struct pci_func *pcif)
{
	// 启用 PCI 设备，系统会自动配内存、IO 端口和 IRQ 中断号
	pci_func_enable(pcif);
    // Device Status Register
    e1000 = (volatile uint32_t *) mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("E1000 status: 0x%08x\n", e1000[E1000_STATUS / sizeof(uint32_t)]);
	
	return 0;
}