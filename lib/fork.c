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
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if ((err & FEC_WR) == 0) {
		panic("pgfault: faulting address %x wasn't a write", addr);
	}
	uint32_t pg_num = (uint32_t) ROUNDDOWN(addr, PGSIZE)/PGSIZE;
	if (!(uvpt[pg_num] & PTE_COW)) {
		panic("pgfault: faulting address %x is not on a copy-on-write page", addr);
	}
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data frm the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	
	// LAB 4: Your code here.
	int perm = PTE_U | PTE_W | PTE_P;
	r = sys_page_alloc(0, (void *) PFTEMP, perm);
	if (r < 0) {
		panic("pgfault: cannot allocate new page");
	}

	memmove(PFTEMP,(void *) ROUNDDOWN(addr, PGSIZE), PGSIZE);
	
	r = sys_page_map(0, (void *) PFTEMP, 0,(void *) ROUNDDOWN(addr, PGSIZE), perm);
	if (r < 0) {
		panic("pgfault: cannot map new page");
	}	
}

//
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
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	int perm = PTE_P | PTE_COW;
	void *va = (void *) (pn * PGSIZE);
	// LAB 4: Your code ere.
	if (uvpt[pn] & PTE_W || uvpt[pn] & PTE_COW) {
		if (uvpt[pn] & PTE_U) {
			perm |= PTE_U;
		}

		r = sys_page_map(thisenv->env_id, va, envid, va, perm);
		if (r < 0) {
			panic("duppage: cannot map parent to child");
		}

		r = sys_page_map(thisenv->env_id, va, thisenv->env_id, va, perm);
		if (r < 0) {
			panic("duppage: cannot parent to parent");
		}
	} else {
		r = sys_page_map(thisenv->env_id, va, envid, va, uvpt[pn] & 0xFFF);
		if (r < 0) {
			panic("duppage: cannot map parent to child");
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
	envid_t child;

	set_pgfault_handler(pgfault);

	child = sys_exofork();

	if (child < 0) {
		panic("sys_exofork: %e", child);
	}
	// We're in the child now
	if (child == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}


	pte_t *pte;
	for (uint32_t i = 0; i < PGNUM(UTOP-PGSIZE); i++) {
		uint32_t pdx = ROUNDDOWN(i,NPDENTRIES)/NPDENTRIES;
		if ((uvpd[pdx] & PTE_P) == PTE_P && (uvpt[i] & PTE_P) == PTE_P) {
			duppage(child, i);
		}
	}

	int r;

	r = sys_page_alloc(child, (void *) (UXSTACKTOP-PGSIZE), PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("fork: cant allocate page");
	}
	r = sys_env_set_pgfault_upcall(child, pgfault);
	if (r < 0) {
		panic("fork: failed setting pagefault");
	}

	r = sys_env_set_status(child, ENV_RUNNABLE);
	if (r < 0) {
		panic("fork: failed setting status");
	}
	return child;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
