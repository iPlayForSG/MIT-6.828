# Question 1

1. 当你随后从一个环境切换到另一个环境时，是否还需要做其他事情来确保这个 I/O 权限设置被正确保存和恢复？为什么？

不需要做其他事情了。进程的状态是保存在每个环境的 `struct Trapframe` 里面的，EFLAGS 寄存器也是 Trapframe 的一部分：`env_tf.tf_eflags`。

当我们发生中断或进行系统调用而陷入内核时，硬件会自动把当前的 EFLAGS 压入内核栈中保存起来。当我们调用 env_run 切换回任意进程时，会调用 env_pop_tf，其中的 iret 指令会自动把栈里保存的 EFLAGS 弹出并恢复到 CPU 寄存器中。 所以，IOPL 的状态完全跟着 Trapframe 的状态进行保存与恢复，不需要再去写额外的代码来单独维护它。