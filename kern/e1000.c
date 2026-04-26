#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>

// LAB 6: Your driver code here

volatile uint32_t *e1000;

// 声明成全局数组，保证物理内存连续
struct tx_desc tx_ring[TX_RING_SIZE];
char tx_bufs[TX_RING_SIZE][TX_PKT_SIZE];

struct rx_desc rx_ring[RX_RING_SIZE];
char rx_bufs[RX_RING_SIZE][RX_PKT_SIZE];

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

static void
e1000_rx_init()
{
	int i;
	// 设置 MAC 52:54:00:12:34:56
	e1000[E1000_RAL / 4] = 0x12005452;
	// 设置高 16 位，并开启第 31 位 Address Valid, 0x80000000
	e1000[E1000_RAH / 4] = 0x5634 | 0x80000000;

	// 清空组播表 MTA
	for (i = 0; i < 128; i++) {
		e1000[(E1000_MTA / 4) + i] = 0;
	}

	memset(rx_ring, 0, sizeof(rx_ring));
	for (i = 0; i < RX_RING_SIZE; i++) {
		rx_ring[i].addr = PADDR(rx_bufs[i]);
		// 接收的时候不用设置状态位，硬件填满了会自动置位
	}

	//  Base Addr
	e1000[E1000_RDBAL / 4] = PADDR(rx_ring);
	e1000[E1000_RDBAH / 4] = 0;

    // Length
	e1000[E1000_RDLEN / 4] = sizeof(rx_ring);

	// Head & Tail，RDT 别置 0
	e1000[E1000_RDH / 4] = 0;
	e1000[E1000_RDT / 4] = RX_RING_SIZE - 1;

	// Receive Control
	// EN: 开启 | BAM: 接收广播 | SECRC: 硬件自动去除 CRC 校验和
	e1000[E1000_RCTL / 4] = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
}

int
e1000_transmit(const void *data, size_t len)
{
	uint32_t tail = e1000[E1000_TDT / 4];

	// 检查该描述符是否空闲：DD 位是否为 1
	if (!(tx_ring[tail].status & E1000_TXD_STAT_DD)) {
		// 队列满了，让用户态重试
		return -1;
	}

	// 限制发送长度
	if (len > TX_PKT_SIZE) {
		len = TX_PKT_SIZE;
	}
	memmove(tx_bufs[tail], data, len);

	tx_ring[tail].length = (uint16_t)len;
	// RS: 发送完设置 DD 位 | EOP: 这是一个完整包的结尾
	tx_ring[tail].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
	tx_ring[tail].status = 0;

	// 更新 TDT 寄存器，注意环形队列取模
	e1000[E1000_TDT / 4] = (tail + 1) % TX_RING_SIZE;

	return 0;
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
    e1000_rx_init();

    // char *test = "MIT 6.828 By iPlayForSG";
    // e1000_transmit(test, 17);
    // e1000_transmit(test, 17);
	return 0;
}

static uint32_t next_rx_idx = 0;

int
e1000_receive(void *addr, size_t max_len)
{
	// 检查当前描述符里有没有数据
	if (!(rx_ring[next_rx_idx].status & E1000_RXD_STAT_DD)) {
		return -1;
	}

	// 获取实际接收到的包长度
	size_t rx_len = rx_ring[next_rx_idx].length;
	if (rx_len > max_len) {
		rx_len = max_len;
	}

	// 将数据从 DMA 缓冲区复制到指定的内存 addr 中
	memmove(addr, rx_bufs[next_rx_idx], rx_len);

	rx_ring[next_rx_idx].status = 0;

	// 将网卡的 RDT 指针更新到刚刚读完的这个位置
	e1000[E1000_RDT / 4] = next_rx_idx;

	// index 往后推 1
	next_rx_idx = (next_rx_idx + 1) % RX_RING_SIZE;

	return rx_len;
}