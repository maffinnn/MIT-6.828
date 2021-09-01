// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	// ����ָ��ָ��commands
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Backtrace the kernel stack", mon_backtrace },
	{ "showmapping", "Display physical page mappings for a given range of virtual addresses", mon_showmapping },
	{ "chmod", "Set, clear or change the permissions of a given range of virtual addresses", mon_chmod },
	{ "dump", "Dump the contents of a range of memory given either a virtual or physical address range", mon_dump },
	//{ "step", "single-step one instruction at a time for debugging use\n", mon_step }
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
	// Your code here.
	/*            
	 *					STACK
	 *		+--   +~~~~~~~~~~~~~~~+
	 *		|	    |          		|
	 *		|		|           	|
	 *		Caller	+---------------+		
	 *		Frame	|  Arguments	|
	 *		|		+---------------+		
	 *		|		|Return Address | <-- saved %eip value (the instruction right after call instruction in the caller)
	 *		+--   +---------------+
	 *				|	Old %ebp	| <-- %ebp
	 *				+---------------+
	 *				|				|
	 *				|Saved Registers|
	 *				|		+		|   
	 *				|Local Variables|
	 *				|				| 
	 *				+---------------+		
	 *				|Argument Build	|
	 *				+~~~~~~~~~~~~~~~+ <-- %esp
	 *
	*/

	uint32_t* ebp = (uint32_t*) read_ebp();

	cprintf("Stack backtrace:\n");
	// while loop�ķ���������
	// 		��entry.S�У� 
	//				movl  $0x0,%ebp  # nuke frame pointer
	while(ebp)
	{	
		uint32_t eip = *(ebp+1);
		struct Eipdebuginfo info; 
		debuginfo_eip(eip, &info);
		
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",ebp, eip,
			*(ebp+2), *(ebp+3), *(ebp+4), *(ebp+5), *(ebp+6));
		
		cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip-info.eip_fn_addr);
		
		// *ebp ȡ%ebpָ���old ebpֵ��һ����ַ ��ǿת��ָ�������
		ebp = (uint32_t*)*ebp; 
	}
	
	return 0;
}

// showmapping [va_start, va_end)
int
mon_showmapping(int argc, char** argv, struct Trapframe* tf)
{
	if (argc < 3) { cprintf("Usage: showmapping [virtual addr] [virtual addr]\n"); return 0;}

	pte_t* pg_tbl_entry;
	uint32_t va_start = 0x0, va_end=0x0; 

	va_start = atox(argv[1]);
	va_end = atox(argv[2]);
	va_end = ROUNDDOWN(va_end, PGSIZE);

	cprintf("VA               PA                 PERMISSION\n");
	for (;va_start<=va_end; va_start+=PGSIZE){
		pg_tbl_entry = pgdir_walk(kern_pgdir, (void*)va_start, 0);
		if (pg_tbl_entry == NULL){
			cprintf("%08x         %08x           ---\n", 
						va_start, ~0);
		}
		else{

			cprintf("%08x         %08x           ", 
					va_start,
					PTE_ADDR(pg_tbl_entry[PTX(va_start)]));
			if ((*pg_tbl_entry)&PTE_P) cprintf("p");
			else cprintf("-");
			if ((*pg_tbl_entry)&PTE_W) cprintf("w");
			else cprintf("-");
			if ((*pg_tbl_entry)&PTE_U) cprintf("u");
			else cprintf("-");

			cprintf("\n");
		} 

	}
	return 0;
}

// + add - remove = set
// e.g. chmod +pw va0 --> to add PTE_P and PTE_W to page at va0
int 
mon_chmod(int argc, char** argv, struct Trapframe* tf)
{
	if (argc < 3) { cprintf("Usage: chmod [+/-/=PERMISSIONS] [virtual addr]\n"); return 0;}

	char* p = argv[1];
	uint32_t va = atox(argv[2]);
	va = ROUNDDOWN(va, PGSIZE);

	pte_t* pg_tbl_entry = pgdir_walk(kern_pgdir, (void*)va, 1);// �������������һ��page��createһ��page���޸�Ȩ��
	switch (argv[1][0])
	{
	case '-':
		for(p=p+1;*p!='\0'; p++){
			if (*p=='p')
				*pg_tbl_entry &= ~PTE_P;
			if (*p=='w')
				*pg_tbl_entry &= ~PTE_W;
			if (*p=='u')
				*pg_tbl_entry &= ~PTE_U;
			if (*p=='-') continue;
		}
		break;
	case '=':
		*pg_tbl_entry &= ~0xFFF;
	case '+':
		for(p=p+1;*p!='\0'; p++){
			if (*p=='p')
				*pg_tbl_entry |= PTE_P;
			if (*p=='w')
				*pg_tbl_entry |= PTE_W;
			if (*p=='u')
				*pg_tbl_entry |= PTE_U;
			if (*p=='-') continue;
		}
		break;
	default:
		cprintf("please enter correct command\n");
		break;
	}

	return 0;
}

// dump -v [va0, va1)
// dump -p [pa0, pa1)
int
mon_dump(int argc, char** argv, struct Trapframe* tf)
{
	if (argc < 4) { cprintf("Usage: dump [-v/p] [addr] [addr]\n"); return 0;}

	pte_t* pg_tbl_entry;
	uint32_t va_start, va_end;
	uint64_t* line;

	/* 
	 * ע�⣺
	 * ����p�����ͱ�����unsigned char* �����ǵ���char* 
	 * ԭ����һЩ�ڴ�ֵ�ᳬ��CHAR_MAX i.e. char��range��[-2^8, 2^8-1], ���Զ�sign extensionһ��ffff 
	 * 		 ��Ҫʹ��unsigned char��hold��Щvalue
	*/
	unsigned char* p;
	if (argv[1][1]=='p'){
		// translate physical address into virtual address
		va_start = (uint32_t)KADDR(atox(argv[2]));
		va_end = (uint32_t)KADDR(atox(argv[3]));
	}else{
		va_start = atox(argv[2]);
		va_end = atox(argv[3]);
	}

	va_start = ROUNDDOWN(va_start, PGSIZE);

	// ָ���ڴ���ǵ�ַ ��ָ���ƫ��������type����
	for (; va_start<= ROUNDDOWN(va_end, PGSIZE); va_start+=PGSIZE){
		// ���va�Ƿ�Ϸ� ���Ƿ��������һ��page at va_start
		pg_tbl_entry = pgdir_walk(kern_pgdir, (void*)va_start, 0);
		if (pg_tbl_entry == NULL){ cprintf("illegal access of paged memory at %08x\n", va_start); continue;}
		for (line=(uint64_t*)va_start; (va_start+PGSIZE<=va_end)&&line<(uint64_t*)(va_start+PGSIZE); line++){
			// ��ӡһ��ÿ8bytes�ĵ�ַ
			cprintf("%08x  ", line);
			for (p=(unsigned char*)line; p<(unsigned char*)(line+1); p++){
				cprintf("%02x ", *p);
			}
			cprintf("\n");
		}
	}

	return 0;
}

// int 
// mon_step(int argc, char** argv, struct Trapframe* tf)
// {
// 	uint32_t eflags = read_eflags();
// 	// enable Trap flag
// 	write_eflags(eflags|FL_IF);
	
// 	return 0;
// }

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
	if (argc == 0){
		return 0;
	}
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0){
			return commands[i].func(argc, argv, tf);
		}
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
	// һ��ʼ�����Ҫ��������required�Ĵ��� ����printf.c��д��main�������Ǳ����������Ҳ���ͷ�ļ�
	// �ο���clann24/jos/lab1��implementation
	// ���Խ�Ҫ���Ե�cprintf�Ĵ��������������(GREAT IDEA!!ѧϰ��!!)
	// int x =1, y =3, z = 4; // inserted
	// cprintf("x %d, y %x, z %d\n", x, y, z); // inserted
	// unsigned int i = 0x00646c72; // inserted
	// cprintf("H%x Wo%s\n", 57616, &i); // inserted
	// cprintf("x=%d y=%d\n", 3); // inserted

	// ����
	// mon_backtrace(0, 0, 0);
	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}


