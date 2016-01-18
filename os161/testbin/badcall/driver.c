#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include "config.h"
#include "test.h"

////////////////////////////////////////////////////////////

static
int
finderror(int rv, int error)
{
	if (rv==-1) {
		return error;
	}
	else {
		return 0;
	}
}

void
report_survival(int rv, int error, const char *desc)
{
	/* allow any error as long as we survive */
	errno = finderror(rv, error);
	warn("passed: %s", desc);
}

void
report_test(int rv, int error, int right_error, const char *desc)
{
	int goterror = finderror(rv, error);

	if (goterror == right_error) {
		warnx("passed: %s", desc);
	}
	else if (goterror == EUNIMP || goterror == ENOSYS) {
		warnx("------: %s (unimplemented)", desc);
	}
	else {
		errno = goterror;
		warn("FAILURE: %s", desc);
	}
}

void
report_test2(int rv, int error, int okerr1, int okerr2, const char *desc)
{
	int goterror = finderror(rv, error);
	if (goterror == okerr1 || goterror == okerr2) {
		warnx("passed: %s", desc);
	}
	else if (goterror == EUNIMP || goterror == ENOSYS) {
		warnx("------: %s (unimplemented)", desc);
	}
	else {
		errno = goterror;
		warn("FAILURE: %s", desc);
	}
}

////////////////////////////////////////////////////////////

int
open_testfile(const char *string)
{
	int fd, rv;
	size_t len;

	fd = open(TESTFILE, O_RDWR|O_CREAT|O_TRUNC);
	if (fd<0) {
		warn("UH-OH: creating %s: failed", TESTFILE);
		return -1;
	}

	if (string) {
		len = strlen(string);
		rv = write(fd, string, len);
		if (rv<0) {
			warn("UH-OH: write to %s failed", TESTFILE);
			close(fd);
			remove(TESTFILE);
			return -1;
		}
		if ((unsigned)rv != len) {
			warnx("UH-OH: write to %s got short count", TESTFILE);
			close(fd);
			remove(TESTFILE);
			return -1;
		}
		rv = lseek(fd, 0, SEEK_SET);
		if (rv<0) {
			warn("UH-OH: rewind of %s failed", TESTFILE);
			close(fd);
			remove(TESTFILE);
			return -1;
		}
	}
	return fd;
}

int
create_testfile(void)
{
	int fd, rv;

	fd = open_testfile(NULL);
	if (fd<0) {
		return -1;
	}

	rv = close(fd);
	if (rv<0) {
		warn("UH-OH: closing %s failed", TESTFILE);
		return -1;
	}

	return 0;
}

int
create_testdir(void)
{
	int rv;
	rv = mkdir(TESTDIR, 0775);
	if (rv<0) {
		warn("UH-OH: mkdir %s failed", TESTDIR);
		return -1;
	}
	return 0;
}

int
create_testlink(void)
{
	int rv;
	rv = symlink("blahblah", TESTLINK);
	if (rv<0) {
		warn("UH-OH: making symlink %s failed", TESTLINK);
		return -1;
	}
	return 0;
}

////////////////////////////////////////////////////////////

static
struct {
	int ch;
	int asst;
	const char *name;
	void (*f)(void);
} ops[] = {
	{ 'a', 2, "execv",		test_execv },
	{ 'b', 2, "waitpid",		test_waitpid },
	{ 'c', 2, "open",		test_open },
	{ 'd', 2, "read",		test_read },
	{ 'e', 2, "write",		test_write },
	{ 'f', 2, "close",		test_close },
	{ 'g', 0, "reboot",		test_reboot },
	{ 'h', 3, "sbrk",		test_sbrk },
	{ 'i', 5, "ioctl",		test_ioctl },
	{ 'j', 2, "lseek",		test_lseek },
	{ 'k', 4, "fsync",		test_fsync },
	{ 'l', 4, "ftruncate",		test_ftruncate },
	{ 'm', 4, "fstat",		test_fstat },
	{ 'n', 4, "remove",		test_remove },
	{ 'o', 4, "rename",		test_rename },
	{ 'p', 5, "link",		test_link },
	{ 'q', 4, "mkdir",		test_mkdir },
	{ 'r', 4, "rmdir",		test_rmdir },
	{ 's', 2, "chdir",		test_chdir },
	{ 't', 4, "getdirentry",	test_getdirentry },
	{ 'u', 5, "symlink",		test_symlink },
	{ 'v', 5, "readlink",		test_readlink },
	{ 'w', 2, "dup2",		test_dup2 },
	{ 'x', 5, "pipe",		test_pipe },
	{ 'y', 5, "__time",		test_time },
	{ 'z', 2, "__getcwd",		test_getcwd },
	{ '{', 5, "stat",		test_stat },
	{ '|', 5, "lstat",		test_lstat },
	{ 0, 0, NULL, NULL }
};

#define LOWEST  'a'
#define HIGHEST '|'

static
void
menu(void)
{
	int i;
	for (i=0; ops[i].name; i++) {
		printf("[%c] %-24s", ops[i].ch, ops[i].name);
		if (i%2==1) {
			printf("\n");
		}
	}
	if (i%2==1) {
		printf("\n");
	}
	printf("[1] %-24s", "asst1");
	printf("[2] %-24s\n", "asst2");
	printf("[3] %-24s", "asst3");
	printf("[4] %-24s\n", "asst4");
	printf("[*] %-24s", "all");
	printf("[!] %-24s\n", "quit");
}

static
void
runit(int op)
{
	int i, k;

	if (op=='!') {
		exit(0);
	}

	if (op=='?') {
		menu();
		return;
	}

	if (op=='*') {
		for (i=0; ops[i].name; i++) {
			printf("[%s]\n", ops[i].name);
			ops[i].f();
		}
		return;
	}

	if (op>='1' && op <= '4') {
		k = op-'0';
		for (i=0; ops[i].name; i++) {
			if (ops[i].asst <= k) {
				printf("[%s]\n", ops[i].name);
				ops[i].f();
			}
		}
		return;
	}

	if (op < LOWEST || op > HIGHEST) {
		printf("Invalid request %c\n", op);
		return;
	}

	ops[op-'a'].f();
}
	
int
main(int argc, char **argv)
{
	int op, i, j;

	printf("[%c-%c, 1-4, *, ?=menu, !=quit]\n", LOWEST, HIGHEST);

	if (argc > 1) {
		for (i=1; i<argc; i++) {
			for (j=0; argv[i][j]; j++) {
				printf("Choose: %c\n",
				       argv[i][j]);
				runit(argv[i][j]);
			}
		}
	}
	else {
		menu();
		while (1) {
			printf("Choose: ");
			op = getchar();
			if (op==EOF) {
				break;
			}
			printf("%c\n", op);
			runit(op);
		}
	}

	return 0;
}
