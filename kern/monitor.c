// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/pmap.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display backtrace", mon_backtrace },
	{ "showmappings", "Display mappings between physical address and virtual address", mon_showmappings }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t* ebp = (uint32_t*)read_ebp();
	struct Eipdebuginfo info;
	memset(&info, 0, sizeof(struct Eipdebuginfo));
	int i;
	while(ebp){
		uint32_t eip = *(ebp + 1);
		cprintf("ebp %08x eip %08x args", ebp, eip);
		for(i = 0; i < 5; i++){
			cprintf(" %08x", *(ebp + 2 + i));
		}
		cprintf("\n");
		if(debuginfo_eip(eip, &info) == 0){
			cprintf("%s:%d: ", info.eip_file, info.eip_line);
			cprintf("%.*s", info.eip_fn_namelen, info.eip_fn_name);
			cprintf("+%d", eip - info.eip_fn_addr);
			cprintf("\n");
		}
		ebp = (uint32_t*)(*ebp);
	}
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t begin, end;
	char* endptr;
	pte_t pte, *pte_p;
	char perm[10] = "P/U/W";
	size_t perm_len = strlen(perm);
	const uint32_t PERM[10] = { PTE_P, PTE_U, PTE_W };
	if (argc == 1 || argc > 3) {
		cprintf("Usage: showmappings BEGIN [END]\n");	
	} else if (argc > 1) {
		begin = ROUNDDOWN((uint32_t)strtol(argv[1], &endptr, 16), PGSIZE);
		if (*endptr != '\0') {
			cprintf("Wrong address!");
			return 0;
		}
		if (argc == 2) {
			end = begin + PGSIZE;
		} else {
			end = ROUNDUP((uint32_t)strtol(argv[2], &endptr, 16), PGSIZE);
			if (*endptr != '\0') {
				cprintf("Wrong address!");
				return 0;
			}
		}
		cprintf("Virtual\tPhysical\tPermission\n");
		for (; begin < end; begin += PGSIZE) {
			struct PageInfo *pp = page_lookup(kern_pgdir, (void *)begin, &pte_p);
			if (pp != NULL) {
				pte = *pte_p;
				uint32_t i;
				for (i = 0; i < perm_len; i += 2) {
					if (!(PERM[i / 2] & pte)) {
						perm[i] = 'X';
					}
				}
				cprintf("%08x\t%08x\t%s\n", begin, page2pa(pp), perm);
			} else {
				cprintf("%08x\t%s\t%s\n", begin, "NULL", "NULL");
			}
		}
	}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
