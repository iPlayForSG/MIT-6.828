#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

	// 为 nsipcbuf 申请一个物理页，确保进行 IPC 页面共享时，内存的权限配置完成
	sys_page_alloc(0, &nsipcbuf, PTE_P | PTE_U | PTE_W);

	int len;
	while (1) {
		// 从 DMA 环形队列里收一个包
		len = sys_pkt_recv(nsipcbuf.pkt.jp_data, 2048);

		if (len < 0) {
			sys_yield();
			continue;
		}
		nsipcbuf.pkt.jp_len = len;
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_U | PTE_W);
		// 下一轮使用
		sys_page_alloc(0, &nsipcbuf, PTE_P | PTE_U | PTE_W);
	}
}
