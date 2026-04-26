#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

extern volatile uint32_t *e1000;

// Device Status Register, offset 8 bytes
#define E1000_STATUS   0x00008

// Transmit 相关寄存器偏移量
#define E1000_TCTL     0x00400  // Transmit Control
#define E1000_TIPG     0x00410  // Transmit Inter-packet gap
#define E1000_TDBAL    0x03800  // TX Descriptor Base Address Low
#define E1000_TDBAH    0x03804  // TX Descriptor Base Address High
#define E1000_TDLEN    0x03808  // TX Descriptor Length
#define E1000_TDH      0x03810  // TX Descriptor Head
#define E1000_TDT      0x03818  // TX Descriptor Tail

// TCTL 寄存器配置位掩码
#define E1000_TCTL_EN       (1 << 1)      // Transmit Enable
#define E1000_TCTL_PSP      (1 << 3)      // Pad Short Packets
#define E1000_TCTL_CT       (0x10 << 4)   // Collision Threshold
#define E1000_TCTL_COLD     (0x40 << 12)  // Collision Distance (Full Duplex)

// 队列大小
#define TX_RING_SIZE  64
#define TX_PKT_SIZE   1518  // 以太网标准最大帧长

// 发送描述符结构体 16 bytes
struct tx_desc
{
	uint64_t addr;      // Buffer address
	uint16_t length;    // Packet length
	uint8_t cso;        // Checksum Offset
	uint8_t cmd;        // Command
	uint8_t status;     // Status
	uint8_t css;        // Checksum Start
	uint16_t special;
} __attribute__((packed));  // 防止编译器内存对齐

// 描述符状态和命令宏
#define E1000_TXD_STAT_DD  0x01   // Descriptor Done
#define E1000_TXD_CMD_EOP  0x01   // End of Packet
#define E1000_TXD_CMD_RS   0x08   // Report Status


int e1000_attach(struct pci_func *pcif);

int e1000_transmit(const void *data, size_t len);

#endif  // SOL >= 6
