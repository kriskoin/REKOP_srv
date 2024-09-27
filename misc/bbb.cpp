#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#define _FL __FILE__,__LINE__

extern "C" {


int main()
{
	printf("__FILE__: %s\n", __FILE__);
	printf("%d\n", __LINE__);
}

}
