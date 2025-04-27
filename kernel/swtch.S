# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	
# 译：上下文切换
# 函数原型如下：void swtch(struct context *old, struct context *new);
# 向old指向的context存放寄存器，从new指向的context加载寄存器


.globl swtch
swtch:
        # 向old指向的context存放寄存器
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)
        #----------------以上是汇编的前半段---------------------
	#----------------以下是汇编的后半段---------------------
	# 从new指向的context加载寄存器
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)

        /******重要重要！！！！！！*/
        # 注意：因为修改了ra寄存器，这里执行ret
        # 之后，会切换到新线程上次被调度出去的地方继续执行
        # 同时因为修改了sp寄存器，内核栈也切换到了新的进程内核栈
        
        ret

	
