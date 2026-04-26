#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	envid_t whom;
	int perm;
	int32_t req;

	while (1) {
		req = ipc_recv(&whom, &nsipcbuf, &perm);

		// 只处理发自核心网络环境，且类型为输出请求的消息
		if (req != NSREQ_OUTPUT || whom != ns_envid) {
			continue;
		}

		struct jif_pkt *pkt = &(nsipcbuf.pkt);
		while (sys_pkt_send(pkt->jp_data, pkt->jp_len) < 0) {
			sys_yield();
		}
	}
}
