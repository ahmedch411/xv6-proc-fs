typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef unsigned long addr_t;

typedef addr_t pde_t;
typedef addr_t pml4e_t;
typedef addr_t pdpe_t;
