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


// Receive 相关寄存器偏移量
#define E1000_RAL      0x05400  // Receive Address Low
#define E1000_RAH      0x05404  // Receive Address High
#define E1000_MTA      0x05200  // Multicast Table Array
#define E1000_RDBAL    0x02800  // RX Descriptor Base Address Low
#define E1000_RDBAH    0x02804  // RX Descriptor Base Address High
#define E1000_RDLEN    0x02808  // RX Descriptor Length
#define E1000_RDH      0x02810  // RX Descriptor Head
#define E1000_RDT      0x02818  // RX Descriptor Tail
#define E1000_RCTL     0x00100  // Receive Control

// RCTL 寄存器配置位掩码
#define E1000_RCTL_EN     (1 << 1)    // Receiver Enable
#define E1000_RCTL_BAM    (1 << 15)   // Broadcast Accept Mode
#define E1000_RCTL_SECRC  (1 << 26)   // Strip Ethernet CRC

// 队列大小
#define RX_RING_SIZE  128
#define RX_PKT_SIZE   2048

// 接收描述符结构体 16 bytes
struct rx_desc
{
	uint64_t addr;      // Buffer address
	uint16_t length;    // Packet length
	uint16_t csum;      // Checksum
	uint8_t status;     // Status
	uint8_t errors;     // Errors
	uint16_t special;
} __attribute__((packed));

// 接收状态位
#define E1000_RXD_STAT_DD       0x01    // Descriptor Done
#define E1000_RXD_STAT_EOP      0x02    // End of Packet

int e1000_attach(struct pci_func *pcif);

int e1000_transmit(const void *data, size_t len);

int e1000_receive(void *addr, size_t max_len);

#endif  // SOL >= 6
