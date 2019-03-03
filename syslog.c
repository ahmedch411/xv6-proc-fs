#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  int fd;

  if(argc != 2){
    printf(1, "expected format: syslog [message]\n");
    exit();
  }

//  printf(1, "%s\n", argv[1]);

  
  if((fd = open("/proc/syslog", O_RDWR)) < 0){
    printf(1, "syslog: cannot open /proc/syslog\n");
    exit();
  }

//  printf(1, "fd: %d\n", fd);

  if (write(fd, argv[1], 10) == -1) {
    printf(1, "syslog: write failed to /proc/syslog\n");
    exit();    
  }
  
  exit();
}
