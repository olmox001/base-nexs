

21:55 (42 minuti fa)
a me

**ROLE & MISSION**
You are Claude Code. Your objective is to perform a complete architectural overhaul, debugging, and implementation of the "NEXS" project (a dynamically-typed, Plan 9-inspired scripting language and baremetal OS). You must supervise, debug, restructure, correct, test, clean up, and execute a complete upgrade of the codebase during your execution process. Do not make assumptions; strictly follow the architecture specifications defined below.
**INITIAL DIRECTIVES**
1. Read PLAN.md to understand the previous planning state.
2. Analyze and supervise the entire existing codebase.
3. Reorganize the project structure completely.
4. Separate and reorganize the Hardware Abstraction Layer (HAL) and the libc implementation, cleanly dividing the baremetal environment from the host environment, and both from the parser.
**ARCHITECTURE & IMPLEMENTATION SPECIFICATIONS**
**1. Memory Management (MMU, Paging & Buddy Allocator)**
* Transition from the current single-block (4KB) allocation to a full MMU abstraction for dynamic paging.
* Map and utilize maximum RAM from BIOS, dividing cache types on baremetal, and growing dynamically on the host.
* Divide memory into fixed logical "blocks" of 128MB/256MB.
* Implement a dual-memory approach:
* "Fast" memory: Managed via the 128/256MB blocks.
* "Contiguous Buffer" memory: Dedicated entirely to I/O operations, updated via a pull-request service.
**2. CPU Core Management & Concurrency**
* Each CPU core must work on only one memory block at a time.
* CPU0 (and CPU1 as support if necessary) acts as the primary router, managing the asynchronous pull-requests and I/O wait states of the other cores.
* All inter-block and inter-core operations must be handled strictly via the existing Mach-style message passing protocol and zero-copy pointers.
**3. Virtual File System & Registry (Plan 9 Protocol)**
* The system acts as a registry editor functioning on a 9P base.
* The root registry (System Block) is mapped strictly to /sys.
* Implement automatic PID-based namespacing. When a program is executed (e.g., via exec()), it must create a new virtual block (defined in .nx or default) mapped in memory.
* All functions, environments, and variables of a loaded program (PID) must reside under its specific node in its respective block.
* Implement scope/branching for variables and functions: If a process is spawned by another, it uses the parent's variables/pointers; otherwise, it forks a new branch or uses the global one.
* Implement a tree-based cleanup mechanism (Garbage Collection) tied to the PID lifecycle.
* **Filesystem Layout:**
* /sys -> Runtime / Root Registry.
* /mnt/sd00 -> Drivers.
* /mnt/sd01 -> System Files.
* /mnt/sd02 to /mnt/sd98 -> Home/User directories.
* Implement Security Rings: Ring 0 and Ring 1. Accessing Ring 0 requires specific authorization flags within the registry.
**4. Language, Runtime & Bitcode**
* The NEXS interpreter/runtime must remain universal, stable, and simple in its core logic (C/C++). All high-level complexity (drivers, filesystems, libraries) must be written in .nx files.
* Ensure the .nx programs execute identically on Host systems and Baremetal implementations.
* Upgrade the current WebAssembly/Bytecode implementation: Create a new file format by encoding .nx programs into specific **Bitcode**.
* Memory and files must be treatable directly as Bitcode. Update this mechanism while debugging the HAL.
**5. Standalone Build & Persistence**
* Improve the mount system.
* When generating a standalone executable/image (e.g., minios), separate the Kernel (Runtime + NEXS interpreter) from the attached programs/filesystem into two distinct partitions.
* The builder must parse the programs to be loaded (using an exec preloader), translate them into Bitcode, and inject them into the executable filesystem partition via 9P pull requests and block paging.
* Implement state persistence: Use a writable partition (USB/Disk on baremetal, or direct host file) to save the Bitcode state (acting as a runtime cache). Add built-in functions to save the state and automatically restore it upon boot.
**EXECUTION INSTRUCTIONS**
Execute the tasks strictly in this order:
1. **Analyze & Reorganize:** Review the code, split Host/Baremetal/Parser, and rewrite the libc integration.
2. **Memory & CPU:** Implement the MMU, Block system, Zero-copy IPC, and CPU routing.
3. **Registry & VFS:** Implement PID namespaces, Ring 0/1 permissions, and the /sys vs /mnt hierarchy.
4. **Bitcode & Build Pipeline:** Upgrade the bitcode generator and implement the dual-partition standalone build system with state persistence.
5. **Debug & Test:** Ensure all built-ins map correctly to both Host and Baremetal. Resolve any bugs encountered during the hal/runtime execution. Do not simplify the implementation; build it completely.

