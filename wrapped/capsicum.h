#include <stdio.h>
#include <sys/types.h>

int lc_available(void);

void lc_send_to_parent(const char *return_type, const char *function,
		       const char *arg_types, ...);

int lc_is_wrapped(void);

struct lc_capability {
  const char *name;
  void (*invoke)(int fd);
};

int lc_wrap_filter(int (*func)(FILE *in, FILE *out), FILE *in, FILE *out,
		   const struct lc_capability *caps, size_t ncaps);
int lc_fork(const char *executable);
void lc_stop_child(int fd);
void lc_exit_child(int fd);

void lc_make_ap(va_list *ap, ...);

void lc_write_void(int fd);
void lc_write_int(int fd, int n);
void lc_write_uint32_t(int fd, uint32_t n);
void lc_write_uint16_t(int fd, uint16_t n);
void lc_write_size_t(int fd, size_t n);
void lc_write_long(int fd, long n);
void lc_write_file_descriptor(int fd, int fd_to_send);
void lc_write_string(int fd, const char *str);
void lc_write_char_array(int fd, const char *src, size_t size);
int lc_read_int(int fd, int *result);
int lc_read_uint32_t(int fd, uint32_t *result);
int lc_read_uint16_t(int fd, uint16_t *result);
int lc_read_long(int fd, long *result);
int lc_read_size_t(int fd, size_t *result);
int lc_read_string(int fd, char **result, uint32_t max);
int lc_read_char_array(int fd, char **result, size_t size);
int lc_read_void(int fd);
int lc_read_file_descriptor(int fd, int *fd_to_read);
