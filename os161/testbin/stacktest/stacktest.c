#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static int n = 4000;


void
foo(int i)
{
        if (i <= 0)
                return;
        printf("calling foo: n-i = %d, &i = 0x%x\n", n-i, (unsigned int)&i);
        foo(i-1);
}

int
main(int argc, char *argv[])
{
	if (argc > 1) {
                n = atoi(argv[1]);
        }
        printf("n = %d\n", n);
        foo(n);
	return 0;
}
