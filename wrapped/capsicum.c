// Candidates for libcapsicum

#include "capsicum.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capability.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int lc_available(void) {
   static int known;
   static int available;

   if (!known) {
      known = 1;
      cap_rights_t rights;
      if (cap_getrights(0, &rights) == 0 || errno != ENOSYS)
	 available = 1;
   }
   return available;
}

static int lc_wrapped;
int lc_is_wrapped(void) { return lc_wrapped; }

static void lc_panic(const char * const msg) {
  static int panicking;

  if (lc_is_wrapped() && !panicking) {
    panicking = 1;
    lc_send_to_parent("void", "lc_panic", "int string", errno, msg);
    exit(0);
  }
  fprintf(stderr, "lc_panic: %s\n", msg);
  perror("lc_panic");
  exit(1);
}

static
void lc_closeallbut(const int *fds, const int nfds) {
  struct rlimit rl;
  int fd;
  int n;

  if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
    lc_panic("Can't getrlimit");

  for (fd = 0; fd < rl.rlim_max; ++fd) {
    for (n = 0; n < nfds; ++n)
      if (fds[n] == fd)
	goto next;
    if (close(fd) < 0 && errno != EBADF) {
      lc_panic("Can't close");
    }
  next:
    continue;
  }
}   

static int lc_limitfd(int fd, cap_rights_t rights)
{
  int fd_cap;
  int error;
  
  fd_cap = cap_new(fd, rights);
  if (fd_cap < 0)
    return -1;
  if (dup2(fd_cap, fd) < 0) {
    error = errno;
    close(fd_cap);
    errno = error;
    return -1;
  }
  close(fd_cap);
  return 0;
}

// FIXME: Voldemort probably wrote this...
void lc_make_ap(va_list *ap, ...) {
  va_start(*ap, ap);
}

static int lc_parent_fd;

static size_t lc_full_read(int fd, void *buffer, size_t count) {
  size_t n;

  for (n = 0; n < count; ) {
    ssize_t r = read(fd, (char *)buffer + n, count - n);
    if (r == 0)
      return 0;
    if (r < 0)
      lc_panic("full_read");
    n += r;
  }
  return n;
}

#define rw_scalar(type)                    \
void lc_write_##type(int fd, type n) {     \
  fprintf(stderr, "[%d] write " #type ": %u\n", getpid(), (unsigned)n);	\
  if (write(fd, &n, sizeof n) != sizeof n) \
    lc_panic("lc_write_" #type " failed"); \
}                                          \
int lc_read_##type(int fd, type *result) { \
  if (lc_full_read(fd, result, sizeof *result) != sizeof *result) \
    return 0;                              \
  fprintf(stderr, "[%d] read " #type ": %u\n", getpid(), (unsigned)*result); \
  return 1;                                \
}


rw_scalar(int)
rw_scalar(uint32_t)
rw_scalar(uint16_t)
rw_scalar(long)
rw_scalar(size_t)

void lc_write_void(int fd) {
  lc_write_int(fd, 0xdeadbeef);
}

void lc_write_string(int fd, const char *string) {
  uint32_t size = strlen(string);
  fprintf(stderr, "[%d] write string: %s\n", getpid(), string);
  if (write(fd, &size, sizeof size) != sizeof size)
    lc_panic("write failed");
  if (write(fd, string, size) != size)
    lc_panic("write failed");
}

void lc_write_char_array(int fd, const char *src, size_t size) {
  lc_write_size_t(fd, size);
  if (write(fd, src, size) != size)
    lc_panic("write failed");
}

/* size of control buffer to send/recv one file descriptor */
#define CONTROLLEN  CMSG_LEN(sizeof(int))

static struct cmsghdr   *cmptr = NULL;  /* malloc'ed first time */

/*
 * Pass a file descriptor to another process.
 * If fd<0, then -fd is sent back instead as the error status.
 */
void lc_write_file_descriptor(int fd, int fd_to_send) {
  struct iovec    iov[1];
  struct msghdr   msg;
  char            buf[2]; /* send_fd()/recv_fd() 2-byte protocol */

  iov[0].iov_base = buf;
  iov[0].iov_len  = 2;
  msg.msg_iov     = iov;
  msg.msg_iovlen  = 1;
  msg.msg_name    = NULL;
  msg.msg_namelen = 0;
  if (fd_to_send < 0) {
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    buf[1] = -fd_to_send;   /* nonzero status means error */
    if (buf[1] == 0)
      buf[1] = 1; /* -256, etc. would screw up protocol */
  } else {
    if (cmptr == NULL && (cmptr = malloc(CONTROLLEN)) == NULL)
      lc_panic("malloc");
    cmptr->cmsg_level  = SOL_SOCKET;
    cmptr->cmsg_type   = SCM_RIGHTS;
    cmptr->cmsg_len    = CONTROLLEN;
    msg.msg_control    = cmptr;
    msg.msg_controllen = CONTROLLEN;
    *(int *)CMSG_DATA(cmptr) = fd_to_send;     /* the fd to pass */
    buf[1] = 0;          /* zero status means OK */
  }
  buf[0] = 0;              /* null byte flag to recv_fd() */
  if (sendmsg(fd, &msg, 0) != 2)
    lc_panic("sendmsg");
}

int lc_read_string(int fd, char **result, uint32_t max) {
  uint32_t size;

  // FIXME: check for errors
  if (lc_full_read(fd, &size, sizeof size) != sizeof size)
    return 0;
  if (size > max)
    lc_panic("oversized string read");
  *result = malloc(size + 1);
  size_t n = lc_full_read(fd, *result, size);
  if (n != size)
    lc_panic("string read failed");
  (*result)[size] = '\0';
  fprintf(stderr, "[%d] Read string: %s\n", getpid(), *result);
  return 1;
}

int lc_read_char_array(int fd, char **result, size_t expected_size) {
  size_t size;

  if (!lc_read_size_t(fd, &size))
    return 0;
  fprintf(stderr, "[%d] Read array size %zd\n", getpid(), size);
  assert(size == expected_size);
  *result = malloc(size);
  size_t n = lc_full_read(fd, *result, size);
  if (n != size)
    lc_panic("string read failed");
  return 1;
}

int lc_read_void(int fd) {
  int v;

  if (!lc_read_int(fd, &v))
    return 0;
  assert(v == 0xdeadbeef);
  return 1;
}

/*
 * Receive a file descriptor from a server process.  Also, any data
 * received is passed to (*userfunc)(STDERR_FILENO, buf, nbytes).
 * We have a 2-byte protocol for receiving the fd from send_fd().
 */
int lc_read_file_descriptor(int fd, int *fd_to_read) {
  int             newfd, nr, status;
  char            *ptr;
  char            buf[2];
  struct iovec    iov[1];
  struct msghdr   msg;

  status = -1;
  iov[0].iov_base = buf;
  iov[0].iov_len  = sizeof(buf);
  msg.msg_iov     = iov;
  msg.msg_iovlen  = 1;
  msg.msg_name    = NULL;
  msg.msg_namelen = 0;
  if (cmptr == NULL && (cmptr = malloc(CONTROLLEN)) == NULL)
    return(-1);
  msg.msg_control    = cmptr;
  msg.msg_controllen = CONTROLLEN;
  if ((nr = recvmsg(fd, &msg, 0)) < 0) {
    lc_panic("recvmsg error");
  } else if (nr == 0) {
    lc_panic("connection closed by server");
    return(-1);
  }
  /*
   * See if this is the final data with null & status.  Null
   * is next to last byte of buffer; status byte is last byte.
   * Zero status means there is a file descriptor to receive.
   */
  for (ptr = buf; ptr < &buf[nr]; ) {
    if (*ptr++ == 0) {
      if (ptr != &buf[nr-1])
	lc_panic("message format error");
      status = *ptr & 0xFF;  /* prevent sign extension */
      if (status == 0) {
	if (msg.msg_controllen != CONTROLLEN)
	  lc_panic("status = 0 but no fd");
	newfd = *(int *)CMSG_DATA(cmptr);
      } else {
	newfd = -status;
      }
      nr -= 2;
    }
  }
  if (status >= 0) {    /* final data has arrived */
    *fd_to_read = newfd;  /* descriptor, or -status */
    return 1;
  }
  return 0;
}

void lc_send_to_parent(const char * const return_type,
		       const char * const function,
		       const char * const arg_types,
		       ...) {
  va_list ap;

  assert(lc_is_wrapped());
  va_start(ap, arg_types);
  fprintf(stderr, "Send: %s\n", function);
  lc_write_string(lc_parent_fd, function);
  if (!strcmp(arg_types, "int"))
    lc_write_int(lc_parent_fd, va_arg(ap, int));
  else if (!strcmp(arg_types, "void"))
    /* do nothing */;
  else
    assert(!"unknown arg_types");
  assert(!strcmp(return_type, "void"));
  lc_read_void(lc_parent_fd);
}

static void lc_process_messages(int fd, const struct lc_capability *caps,
				size_t ncaps) {
  for ( ; ; ) {
    char *name;
    size_t n;

    if (!lc_read_string(fd, &name, 100))
      return;

    for (n = 0; n < ncaps; ++n)
      if (!strcmp(caps[n].name, name)) {
	caps[n].invoke(fd);
	goto done;
      }

    fprintf(stderr, "Can't process capability \"%s\"\n", name);
    lc_panic("bad capability");
  done:
    continue;
  }
}

// FIXME: do this some other way, since we can't stop the child from
// using our code.
#define FILTER_EXIT  123
int lc_wrap_filter(int (*func)(FILE *in, FILE *out), FILE *in, FILE *out,
		   const struct lc_capability *caps, size_t ncaps) {
  int ifd,  ofd, pid, status;
  int pfds[2];

  ifd = fileno(in);
  ofd = fileno(out);

  if (pipe(pfds) < 0)
    lc_panic("Cannot pipe");

  if ((pid = fork()) < 0)
    lc_panic("Cannot fork");

  if (pid != 0) {
    /* Parent process */
    close(pfds[1]);
    lc_process_messages(pfds[0], caps, ncaps);

    wait(&status);
    if(WIFEXITED(status)) {
      status = WEXITSTATUS(status);
      if (status != 0 && status != FILTER_EXIT)
	lc_panic("Unexpected child status");
      return status == 0;
    } else {
      lc_panic("Child exited abnormally");
    }
  } else { 
    /* Child process */
    int fds[4];

    lc_wrapped = 1;
    close(pfds[0]);
    lc_parent_fd = pfds[1];
    if(lc_limitfd(ifd, CAP_READ | CAP_SEEK) < 0
       || lc_limitfd(ofd, CAP_WRITE | CAP_SEEK) < 0
       // FIXME: CAP_SEEK should not be needed!
       || lc_limitfd(lc_parent_fd, CAP_READ | CAP_WRITE | CAP_SEEK) < 0) {
      lc_panic("Cannot limit descriptors");
    }
    fds[0] = 2;
    fds[1] = ofd;
    fds[2] = ifd;
    fds[3] = lc_parent_fd;
    lc_closeallbut(fds, 4);

    if (lc_available() && cap_enter() < 0)
      lc_panic("cap_enter() failed");

    if((*func)(in, out)) {
      exit(0);
    } else {
      exit(FILTER_EXIT);
    }
    /* NOTREACHED */
  }
  /* NOTREACHED */
  return 0;
}

int lc_fork(const char *executable) {
  int pid;
  int pfds[2];

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pfds) < 0)
    lc_panic("opening stream socket pair");

  if ((pid = fork()) < 0)
    lc_panic("Cannot fork");

  if (pid != 0) {
    /* Parent process */
    close(pfds[1]);
    return pfds[0];
  } else { 
    /* Child process */
    int fds[2];
    char *argv[3];
    char buf[100];
    extern char **environ;

    close(pfds[0]);
    if(// FIXME: CAP_SEEK should not be needed!
       lc_limitfd(pfds[1], CAP_READ | CAP_WRITE | CAP_SEEK) < 0
       || lc_limitfd(2, CAP_WRITE | CAP_SEEK)) {
      lc_panic("Cannot limit descriptors");
    }
    fds[0] = 2;
    fds[1] = pfds[1];
    lc_closeallbut(fds, 2);

    /* FIXME: The child must do this
    if (lc_available() && cap_enter() < 0)
      lc_panic("cap_enter() failed");
    */

    sprintf(buf, "%d", pfds[1]);

    // FIXME: sigh. cast.
    argv[0] = (char *)executable;
    argv[1] = buf;
    argv[2] = NULL;
    // FIXME: what should we pass for |envp|?
    // FIXME: it is unfortunate that the child gets child_fd
    execve(executable, argv, environ);
    lc_panic("execve failed");
    /* NOTREACHED */
  }
  /* NOTREACHED */
  return 0;
}

void lc_stop_child(int fd) {
  int status;

  lc_write_string(fd, "exit");
  lc_read_void(fd);
  wait(&status);
  if(WIFEXITED(status)) {
    status = WEXITSTATUS(status);
    if (status != 0)
      lc_panic("Unexpected child status");
  } else {
    lc_panic("Child exited abnormally");
  }
}

