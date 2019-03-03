#include "types.h"
#include "param.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fs.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "memlayout.h"
#include "date.h"

#define T_DIR ((1))
#define SYSLOGSIZE ((2000000))
char syslogbuf[SYSLOGSIZE];

static void
sprintuint(char* buf, uint x)
{
  uint stack[10];
  uint stack_size = 0;
  if (x == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  while (x) {
    stack[stack_size++] = x % 10u;
    x /= 10u;
  }
  uint buf_size = 0;
  while (stack_size) {
    buf[buf_size++] = '0' + stack[stack_size - 1];
    stack_size--;
  }
  buf[buf_size] = 0;
}

pte_t * walkpgdir(pde_t *pml4, const void *va, int alloc);
void
procfs_ipopulate(struct inode* ip)
{
  ip->size = 0;
  ip->flags |= I_VALID;

  // inum < 10000 are reserved for directories
  // use inum > 10000 for files in procfs
  ip->type = ip->inum < 10000 ? T_DIR : 100;
}

void
procfs_iupdate(struct inode* ip)
{
}

static int
procfs_writei(struct inode* ip, char* buf, uint offset, uint count)
{
  char space[3];
  space[0] = ' ';
  space[1] = ' ';
  space[2] = '\0';
  char newline[2];
  memset(newline, 10, 1);
  newline[1] = '\0';
  char buf1[32];
  int currlength = strlen(syslogbuf);
  //if inode number is not 10003 or the buffer for syslog is full, then return error
  if (ip->inum != 10003 || currlength + 20 + strlen(buf) > SYSLOGSIZE){
    return -1;
  }
  memset(buf1, 0, 32);
  acquire(&tickslock);
  sprintuint(buf1, ticks);
  release(&tickslock);
  strcat(syslogbuf, buf1);
  strcat(syslogbuf, space);  
  memset(buf1, 0, 32);
  sprintuint(buf1, proc->pid);
  strcat(syslogbuf, buf1);
  strcat(syslogbuf, space);
  strcat(syslogbuf, buf);
  strcat(syslogbuf, newline);  
//  cprintf("write in procfs with current length %d with ticks: %d and number of characters: %d  %d\n", strlen(syslogbuf), ticks, strlen(syslogbuf) - currlength, count);
  return count;
}

static void
sprintx32(char * buf, uint x)
{
  buf[0] = x >> 28;
  for (int i = 0; i < 8; i++) {
    uint y = 0xf & (x >> (28 - (i * 4)));
    buf[i] = (y < 10) ? (y + '0') : (y + 'a' - 10);
  }
  buf[8] = '\0';
}

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

#define PROCFILES ((3))
struct dirent procfiles[PROCFILES+NPROC+1] = {{10001,"meminfo"}, {10002,"cpuinfo"}, {10003, "syslog"}};

// returns the number of active processes, and updates the procfiles table
static uint
updateprocfiles()
{
  int num = 0, index = 0;
  acquire(&ptable.lock);
  while (index < NPROC) {
    if (ptable.proc[index].state != UNUSED && ptable.proc[index].state != ZOMBIE) {
//      cprintf("updateprocfiles: pid is: %d %d\n", ptable.proc[index].pid, PROCFILES+num);
      procfiles[PROCFILES+num].inum = index+1;
      sprintuint(procfiles[PROCFILES+num].name,ptable.proc[index].pid);
      num++;
      if (ptable.proc[index].pid == proc->pid) {
        procfiles[PROCFILES+num].inum = index+1;        
        memmove(procfiles[PROCFILES+num].name, "self", strlen("self"));
        num++;
      }
    }
    index++;
  }
  release(&ptable.lock);
  return PROCFILES + num;
}

static int
readi_helper(char * buf, uint offset, uint maxsize, char * src, uint srcsize)
{
  if (offset > srcsize)
    return -1;
  uint end = offset + maxsize;
  if (end > srcsize)
    end = srcsize;
  memmove(buf, src+offset, end-offset);
  return end-offset;
}

int
procfs_readi(struct inode* ip, char* buf, uint offset, uint size)
{
  const uint procsize = sizeof(struct dirent)*updateprocfiles();
//  cprintf("ip->inum: %d, ip->mounted_dev: %d\n", ip->inum, ip->mounted_dev);
  // the mount point
  if (ip->mounted_dev == 2) {
    return readi_helper(buf, offset, size, (char *)procfiles, procsize);
  }

  // directory - can only be one of the process directories
  if (ip->type == T_DIR) {
    struct dirent procdir[4] = {{20000+ip->inum, "name"}, {30000+ip->inum, "ppid"}, {40000+ip->inum, "pid"}, {50000+ip->inum, "mappings"}};
    return readi_helper(buf, offset, size, (char *)procdir, sizeof(procdir));
  }

  // files
  char buf1[32];
  memset(buf1, 0, 32);
  char newline[2];
  memset(newline, 10, 1);
  newline[1] = '\0';
  
  switch (((int)ip->inum)) {
  case 10001: // meminfo: print the number of free pages
    sprintuint(buf1, kmemfreecount());
    strcat(buf1, newline);    
    return readi_helper(buf, offset, size, buf1, strlen(buf1));
  case 10002: // cpuinfo: print the total number of cpus. See the 'ncpu' global variable
    sprintuint(buf1, ncpu);
    strcat(buf1, newline);    
    return readi_helper(buf, offset, size, buf1, strlen(buf1));
  case 10003: //syslog: print the content of the syslog
    return readi_helper(buf, offset, size, syslogbuf, strlen(syslogbuf));
    default: break;
  }
  //name requested
  if (ip->inum > 20000 && ip->inum < 30000) {
//    cprintf("name requested, name: %s ip->inum: %d, ip->inum-30000: %d\n", ptable.proc[ip->inum-20001].name, ip->inum, ip->inum-20001);    
    acquire(&ptable.lock);
    memmove(buf1, ptable.proc[ip->inum-20001].name, strlen(ptable.proc[ip->inum-20001].name));
    strcat(buf1, newline);    
    release(&ptable.lock);
    return readi_helper(buf, offset, size, buf1, strlen(buf1));    
  }
  //ppid requested
  else if (ip->inum > 30000 && ip->inum < 40000) {
//    cprintf("ppid requested, name: %s ip->inum: %d, ip->inum-30000: %d\n", ptable.proc[ip->inum-30001].name, ip->inum, ip->inum-30001);    
    acquire(&ptable.lock);
    sprintuint(buf1, ptable.proc[ip->inum-30001].parent->pid);
    strcat(buf1, newline);
    release(&ptable.lock);
    return readi_helper(buf, offset, size, buf1, strlen(buf1));    
  }
  //pid requested
  else if (ip->inum > 40000 && ip->inum < 50000) {
//    cprintf("ppid requested, name: %s ip->inum: %d, ip->inum-30000: %d\n", ptable.proc[ip->inum-40001].name, ip->inum, ip->inum-40001);    
    acquire(&ptable.lock);
    sprintuint(buf1, ptable.proc[ip->inum-40001].pid);
    strcat(buf1, newline);
    release(&ptable.lock);
    return readi_helper(buf, offset, size, buf1, strlen(buf1));
  }
  //mappings requested
  else if (ip->inum > 50000 && ip->inum < 60000) {
    pte_t *pte;
    addr_t pa, i;
    char vabuf[8];
    char pabuf[8];
    char finalbuffer[2000];
    finalbuffer[0] = '\0';
    memset(finalbuffer, 0, 2000);
    char space[3];
    space[0] = ' ';
    space[1] = ' ';
    space[2] = '\0';
    char newline[2];
    memset(newline, 10, 1);
    newline[1] = '\0';  
//    cprintf("mappings requested\n %x %d\n", ptable.proc[ip->inum-50001].pgdir, ptable.proc[ip->inum-50001].sz);
    acquire(&ptable.lock);    
    for (i=0; i <  ptable.proc[ip->inum-50001].sz; i += PGSIZE) {
      if ((pte = walkpgdir(ptable.proc[ip->inum-50001].pgdir, (void *) i, 0)) == 0)
        panic("pte should exist");
      pa = PTE_ADDR(*pte);
      //if page table is not present
      if ((*pte & PTE_P) == 0) {
        //cprintf("page not present\n");
        pa = 0;
      }
      sprintx32(pabuf, pa);
      sprintx32(vabuf, i);
      strcat(finalbuffer, vabuf);
      memset(vabuf, 0, 8);
      strcat(finalbuffer, space);
      strcat(finalbuffer, pabuf);
      memset(pabuf, 0, 8);      
      strcat(finalbuffer, space);      
      strcat(finalbuffer, newline);
    }    
    release(&ptable.lock);
    return readi_helper(buf, offset, size, finalbuffer, strlen(finalbuffer));      
    
  }
  return -1; // return -1 on error
}

struct inode_functions procfs_functions = {
  procfs_ipopulate,
  procfs_iupdate,
  procfs_readi,
  procfs_writei,
};

void
procfsinit(char * const path)
{
  begin_op();
  struct inode* mount_point = namei(path);
  if (mount_point) {
    ilock(mount_point);
    mount_point->i_func = &procfs_functions;
    mount_point->mounted_dev = 2;
    iunlock(mount_point);
  }
  memset(syslogbuf, 0, SYSLOGSIZE);
  end_op();
}
