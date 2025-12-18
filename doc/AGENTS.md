# AGENTS.md - Guidelines for AI Agents Working on DragonFly BSD

## Project Context

This repository is the DragonFly BSD kernel source tree. DragonFly BSD is a
Unix-like operating system forked from FreeBSD 4.8, featuring a unique
approach to SMP with LWKT (Light Weight Kernel Threads) and a sophisticated
virtual memory system.

The vkernel (virtual kernel) is a key DragonFly feature that allows running
a full kernel in userspace without hardware virtualization, using the
MAP_VPAGETABLE mechanism for software-managed page tables.

## Critical Rules

### 1. Never Push to Remote
- **NEVER push to the remote repository**
- Commits are allowed, but pushing is strictly forbidden
- Always work on local branches only

### 2. Do Not Modify sys/vm Structures
- **Do NOT modify existing struct layouts** in sys/vm (vm_page, vm_map_entry, vm_object, etc.)
- Do NOT change the size or memory layout of any sys/vm structures
- This would have far-reaching implications across the entire VM subsystem

### 3. What IS Allowed in sys/vm
- **Adding/using flags** is OK (e.g., using existing PG_UNUSED slots)
- **Adding new functions** is OK
- **Adding new code paths** is OK
- These don't change struct layouts, just add functionality

### 4. Favor Platform-Specific Code
- When possible, prefer making changes in platform-specific code over core kernel code
- For vkernel work: `sys/platform/vkernel64/`
- For x86_64 work: `sys/platform/pc64/`

### 5. Do Not Change General Kernel Behavior
- New code paths should only activate for their specific use case
- Normal operations must remain unchanged
- Performance of existing paths must not be affected

## Development Workflow

### DragonFly VM Access
A DragonFly BSD VM is available for testing kernel builds:

```bash
ssh -i /home/antonioh/.go-synth/vm/id_ed25519 -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@localhost
```

### Repository Synchronization
- This repository is cloned in the VM at `/usr/src`
- Changes must be committed locally first, then pulled in the VM with `git pull`
- Workflow:
  1. Make changes in the local repo
  2. Commit changes locally
  3. SSH into the VM
  4. `cd /usr/src && git pull`
  5. Build and test in the VM

### Building the Kernels in the VM
```bash
cd /usr/src

# Build the host kernel
make -j4 buildkernel KERNCONF=X86_64_GENERIC
make installkernel KERNCONF=X86_64_GENERIC
reboot

# Build the vkernel (virtual kernel) - NO_MODULES required
make -j4 buildkernel KERNCONF=VKERNEL64 -DNO_MODULES
# vkernel binary is installed to /var/vkernel/boot/kernel/kernel
```

### Testing the Vkernel
```bash
# Enable vkernel support (required on host)
sysctl vm.vkernel_enable=1

# Run the vkernel (rootimg.01 must exist)
/var/vkernel/boot/kernel/kernel -m 1g -r /var/vkernel/rootimg.01
```

The vkernel files are located in `/var/vkernel/`:
- `boot/kernel/kernel` - the vkernel binary
- `rootimg.01` - root disk image (must be created manually)

## Key Documentation

- `doc/vpagetable_analysis.txt` - MAP_VPAGETABLE technical analysis
- `doc/qcow2_vkernel.txt` - QCOW2 support plan for vkernel disks
- `doc/porting_drivers.txt` - Driver porting guide
