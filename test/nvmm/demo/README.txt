Two additional components are shipped with NVMM as demonstrators, toyvirt and
smallkern: the former is a toy virtualizer, that executes in a VM the 64bit ELF
binary given as argument, the latter is an example of such binary.

The goal of toyvirt and smallkern is to demonstrate the libnvmm API.

Originally obtained from: https://www.netbsd.org/~maxv/nvmm/nvmm-demo.zip

Folders:

	toyvirt/
		A toy virtualizer.

	smallkern/
		A small kernel, that can be booted by toyvirt.

Use:

	$ make
	# /tmp/toyvirt /tmp/smallkern

Expected output:

	[+] NVMM initialization succeeded
	[+] Machine creation succeeded
	[+] VCPU creation succeeded
	[+] VCPU callbacks configuration succeeded
	[+] State set
	mach>     _________               __   __   __
	mach>    /   _____/ _____ _____  |  | |  | |  | __ ___________  ____
	mach>    \_____  \ /     \\__  \ |  | |  | |  |/ // __ \_  __ \/    \
	mach>    /        \  Y Y  \/ __ \|  |_|  |_|    <\  ___/|  | \/   |  \
	mach>   /_______  /__|_|  (____  /____/____/__|_ \\___  >__|  |___|  /
	mach>           \/      \/     \/               \/    \/           \/
	mach>   [+] TSS created
	mach>   [+] IDT created
	mach>   [+] APICBASE is correct
	mach>   [+] PG_NX is disabled
	mach>   [+] Running on cpu120
	mach>   [+] LAPIC information matches
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   privileged instruction fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   non-maskable interrupt
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   hardware interrupt
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	mach>
	mach>   ****** FAULT OCCURRED ******
	mach>   protection fault
	mach>   ****************************
	[+] Machine received shutdown
	[+] Machine execution successful
	[+] Machine destroyed

The VM executes 'vmmcall', which causes a privileged instruction fault.

Toyvirt injects in the VM, at regular intervals, a protection fault (#GP),
followed by a non-maskable interrupt (#NMI), followed by an external
hardware interrupt.

The first NMI blocks further NMIs until 'iret' is executed by the VM.
Given that the VM never executes this instruction, all the secondary NMIs
are blocked. That's why the NMI message gets displayed only once.

The protection faults, however, are not subject to blocking, and are
received all the time.

After receiving the first external hardware interrupt, the VM disables
these interrupts by setting CR8 to 15. From then on, no hardware interrupt
is received.

The VM accepts up to ten faults. Beyond that, it shuts down.

All the while, the VM performs a few FPU operations, and verifies their
correctness.
