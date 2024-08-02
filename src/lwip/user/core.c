#include "lwipopts.h"
#include <stdio.h>
#include <stdlib.h>

//
// lwipopts.h
//
void lwip_example_app_platform_assert(const char *msg, int line, const char *file) {
	printf("Assertion \"%s\" failed at line %d in %s\n", msg, line, file);
	fflush(NULL);
	abort();
}