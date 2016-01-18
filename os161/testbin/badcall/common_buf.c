/*
 * Calls with invalid transfer buffers
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <err.h>

#include "config.h"
#include "test.h"

static int buf_fd;

struct buftest {
	int (*setup)(void);
	int (*op)(void *);
	void (*cleanup)(void);
	const char *name;
};

////////////////////////////////////////////////////////////

static
int
read_setup(void)
{
	buf_fd = open_testfile("i do not like green eggs and ham");
	if (buf_fd<0) {
		return -1;
	}
	return 0;
}

static
int
read_badbuf(void *buf)
{
	return read(buf_fd, buf, 128);
}

static
void
read_cleanup(void)
{
	close(buf_fd);
	remove(TESTFILE);
}

//////////

static
int
write_setup(void)
{
	buf_fd = open_testfile(NULL);
	if (buf_fd<0) {
		return -1;
	}
	return 0;
}

static
int
write_badbuf(void *ptr)
{
	return write(buf_fd, ptr, 128);
}

static
void
write_cleanup(void)
{
	close(buf_fd);
	remove(TESTFILE);
}

//////////

static
int
getdirentry_setup(void)
{
	buf_fd = open(".", O_RDONLY);
	if (buf_fd < 0) {
		warn("UH-OH: couldn't open .");
		return -1;
	}
	return 0;
}

static
int
getdirentry_badbuf(void *ptr)
{
	return getdirentry(buf_fd, ptr, 1024);
}

static
void
getdirentry_cleanup(void)
{
	close(buf_fd);
}

//////////

static
int
readlink_setup(void)
{
	return create_testlink();
}

static
int
readlink_badbuf(void *buf)
{
	return readlink(TESTLINK, buf, 168);
}

static
void
readlink_cleanup(void)
{
	remove(TESTLINK);
}

//////////

static int getcwd_setup(void) { return 0; }
static void getcwd_cleanup(void) {}

static
int
getcwd_badbuf(void *buf)
{
	return __getcwd(buf, 408);
}

////////////////////////////////////////////////////////////

static
void
common_badbuf(struct buftest *info, void *buf, const char *bufdesc)
{
	char mydesc[128];
	int rv;

	snprintf(mydesc, sizeof(mydesc), "%s with %s buffer", 
		 info->name, bufdesc);
	info->setup();
	rv = info->op(buf);
	report_test(rv, errno, EFAULT, mydesc);
	info->cleanup();
}

static
void
any_badbuf(struct buftest *info)
{
	common_badbuf(info, NULL, "NULL");
	common_badbuf(info, INVAL_PTR, "invalid");
	common_badbuf(info, KERN_PTR, "kernel-space");
}

////////////////////////////////////////////////////////////

#define T(call) \
  void					\
  test_##call##_buf(void)		\
  {					\
  	static struct buftest info = {	\
  		call##_setup,		\
  		call##_badbuf,		\
  		call##_cleanup,		\
  		#call,			\
	};				\
   	any_badbuf(&info);		\
  }

T(read);
T(write);
T(getdirentry);
T(readlink);
T(getcwd);
