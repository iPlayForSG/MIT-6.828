# Question 1
1. 首先在内核里分配两个全局数组 tx_ring tx_bufs，它们在 .bss 所以保证内存连续。e1000_transmit 检查队尾描述符的状态，复制数据，并更新尾指针触发硬件发送。

在系统调用层封装 sys_pkt_send，在用户层让 output 作为一个独立的进程运行，通过 IPC 接收核心网络服务器发来的数据包，然后调用系统调用发出去。

如果队列已满，在内核层会直接返回 -1 到用户态，用户态接受到之后，先 sys_yield 让出 CPU 时间片，随后再尝试发送，直到成功发送。

# Question 2

2. 跟 Question 1 其实是一样的，只不过反过来而已。唯一需要注意的是每一轮循环完毕后都要`sys_page_alloc(0, &nsipcbuf, PTE_P | PTE_U | PTE_W);`分配新的页供下一轮循环使用。

