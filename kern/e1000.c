#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>

// LAB 6: Your driver code here

volatile uint32_t *e1000;

// 声明成全局数组，保证物理内存连续
struct tx_desc tx_ring[TX_RING_SIZE];
char tx_bufs[TX_RING_SIZE][TX_PKT_SIZE];

static void
e1000_tx_init()
{
	int i;
	memset(tx_ring, 0, sizeof(tx_ring));
	for (i = 0; i < TX_RING_SIZE; i++) {
		// 给每个描述符绑定一个存放数据的物理缓冲区
		tx_ring[i].addr = PADDR(tx_bufs[i]);
		// 标记为 Descriptor Done，表示网卡已发送完毕，目前空闲
		tx_ring[i].status = E1000_TXD_STAT_DD; 
	}

	// Base Addr 物理地址
	e1000[E1000_TDBAL / 4] = PADDR(tx_ring);
	e1000[E1000_TDBAH / 4] = 0;

    // Length
	e1000[E1000_TDLEN / 4] = sizeof(tx_ring);

	// Head & Tail
	e1000[E1000_TDH / 4] = 0;
	e1000[E1000_TDT / 4] = 0;

	// Transmit Control Register
	// EN: 开启 | PSP: 填充短包 | CT: 0x10 | COLD: 0x40 全双工
	e1000[E1000_TCTL / 4] = E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD;

	// Transmit Inter-packet Gap
	// IEEE 802.3：IPGT = 10, IPGR1 = 8, IPGR2 = 6
	e1000[E1000_TIPG / 4] = 10 | (8 << 10) | (6 << 20);
}

int
e1000_attach(struct pci_func *pcif)
{
	// 启用 PCI 设备，系统会自动配内存、IO 端口和 IRQ 中断号
	pci_func_enable(pcif);
    // Device Status Register
    e1000 = (volatile uint32_t *) mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("E1000 status: 0x%08x\n", e1000[E1000_STATUS / sizeof(uint32_t)]);
	e1000_tx_init();
	return 0;
}