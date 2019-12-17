#include "plan9.h"
#include "../osint.h"

static int
my_read(int f, char *a, long n)
{
    int r;

    r = rx_timeout((uint8_t *)a, (int)n, 1000);
    return r;
}

long
readn(int f, void *av, long n)
{
	char *a;
	long m, t;

	a = av;
	t = 0;
	while(t < n){
		m = my_read(f, a+t, n-t);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
	}
	return t;
}

long
writen(int f, void *av, long n)
{
    return tx((uint8_t *)av, (int)n);
}
