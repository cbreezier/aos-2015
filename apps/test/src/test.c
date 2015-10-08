#include <stdio.h>
#include <fcntl.h>

int main(void) {
    int a, b;

    int in = open("console", O_RDONLY);
    char buf[10];

    read(in, buf, 10);
    sscanf(buf, "%d%d", &a, &b);

    printf("%d\n", a+b);

    return 0;
}
