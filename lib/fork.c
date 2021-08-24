// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
extern int cnt;
static void
pgfault(struct UTrapframe *utf)
{
	// cprintf("[%08x] called pgfault\n", sys_getenvid());
	void *addr = (void *) utf->utf_fault_va;
	// cprintf("utf->fault_va: %08x\n", addr);
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!((err&FEC_WR)&&(uvpd[PDX(addr)]&PTE_P)&&(uvpt[PGNUM(addr)]&PTE_P)&&(uvpt[PGNUM(addr)]&PTE_COW))){
		panic("pgfault: the access is not a write to a copy-on-write page\n");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	// ��PFTEMP�����һ�������ַ��mapping �൱��storeһ�������ַ
	if((r = sys_page_alloc(0, (void*)PFTEMP, PTE_P|PTE_U|PTE_W))<0){
		panic("pgfault: sys_page_alloc failed due to %e\n",r);
	}
	addr = ROUNDDOWN(addr, PGSIZE);
	// �ȸ���addr���content
	memcpy((void*)PFTEMP, addr, PGSIZE);
	// ��remap addr��PFTEMPλ�� ��addr��PFTEMPָ��ͬһ�������ڴ�
	if ((r = sys_page_map(0, (void*)PFTEMP, 0, addr, PTE_P|PTE_U|PTE_W))<0){
		panic("pgfault: sys_page_map failed due to %e\n", r);
	}
	if ((r = sys_page_unmap(0, (void*)PFTEMP))<0){
		panic("pgfault: sys_page_unmap failed due to %e\n", r);
	}
	
	
}

// called by parent
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
// ��page table��ӳ��
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	// LAB 4: Your code here.
	void* addr = (void*)(pn*PGSIZE);
	// cprintf("[%08x] called duppage\nduplicate page at %08x\n", sys_getenvid(), addr);
	int perm;
	/*
	 * file descriptor ��0xd0000000���λ�ü����� 
	 * ���ҽ���һ��fd��in use��״̬ʱ, һ��file descriptor table page�Żᱻӳ��
	 * ������Ҫshare file descriptor state across fork and spawn, 
	 * ����pipe��������ִ�� 
     * �������������COW�Ļ� �õ���fd���Ǹ��� ��ôʹ��ʱ�ͻ������
	*/
	if (uvpt[pn]&PTE_SHARE){
		// cprintf("page at %08x is PTE_SHARE\n", addr);
		r = sys_page_map(0, addr, envid, addr, (uvpt[pn]&PTE_SYSCALL));
		if (r<0) return r;
	}	
	else {
		if (uvpt[pn]&PTE_W||uvpt[pn]&PTE_COW){
		/*
		 * ����f�����̱���Ҫ�����ø��ӽ��̵�mapping����remmap�������Լ������page
		 * ԭ��: ������ӽ���д��page֮ǰ, �����̶����page�����޸�
		 * �ͻᷢ��copy on write(�����page�ᱻ���¸���һ��)�������Ͳ���ı�ԭ����һҳ������
      	 * �����ӽ�����д��ʱ��page�������Ҳ�Ͳ��ᱻcorruped�����ɱ��ָ����̸ո�fork����״̬��
		 * �������ӽ�����ȫ����
		 */
			r = sys_page_map(0, addr, envid, addr, PTE_COW|PTE_U|PTE_P); // not writable
			if (r<0) return r;
			// �޸ĸ����̵�page��Ȩ��
			r = sys_page_map(0, addr, 0, addr, PTE_COW|PTE_U|PTE_P); // not writable
			if (r<0) return r;
		}
		else {
			if ((r=sys_page_map(0, addr, envid, addr, PTE_P|PTE_U)<0))// read only
				return r;
		}
	}

	return 0;

}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// cprintf("[%08x] called fork\n", sys_getenvid());
	int r; uintptr_t addr;
	set_pgfault_handler(pgfault);

	envid_t envid = sys_exofork();
		
	if (envid<0){
		panic("fork: sys_exofork error %e\n", envid);
	}

	if (envid==0){
		//child process
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	
	// duplicate parent address space
	for (addr = 0;addr<USTACKTOP; addr+=PGSIZE){
		if ((uvpd[PDX(addr)]&PTE_P)&&(uvpt[PGNUM(addr)]&PTE_P)){
			duppage(envid, PGNUM(addr));
		}
	}
	// allocate user exception stack
	// cprintf("alloc user expection stack\n");
	if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP-PGSIZE),PTE_P|PTE_W|PTE_U))<0)
		return r;
	
	extern void* _pgfault_upcall();
	sys_env_set_pgfault_upcall(envid, _pgfault_upcall);

	if ((r = sys_env_set_status(envid, ENV_RUNNABLE))<0)
		return r;

	return envid;
	
}

// Challenge(not sure yet)
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;	
}
