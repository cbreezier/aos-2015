/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/* Simple shell to run on SOS */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <utils/time.h>

/* Your OS header file */
#include <sos.h>

#define BUF_SIZ   32768
#define MAX_ARGS   32

static int in;
static sos_stat_t sbuf;

static int sosh_open(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: open <file> <mode>\n");
        return 1;
    }
    
    int fd = open(argv[1], atoi(argv[2]));

    if (fd < 0) {
        printf("Warning warning fd negative\n");
    }

    printf("Your fd for %s is fd %d\n", argv[1], fd);

    return 0;
}

static int sosh_read(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: read - <fd> <offset> <numread>\n");
        return 1;
    }

    char buf[(int)1e6];
    buf[0] = '\0';

    int nread = read(atoi(argv[1]), buf + atoi(argv[2]), atoi(argv[3]));

    if (nread < 0) {
        printf("Warning warning nread = %d\n", nread);
    } else {
        printf("%s\n", buf + atoi(argv[2]));
    }

    return 0;
}   

static int sosh_write(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: write - <fd> <string> <num bytes>\n");
        return 1;
    }

    int nwrite = write(atoi(argv[1]), argv[2], atoi(argv[3]));

    if (nwrite < 0) {
        printf("Warning warning nwrite = %d\n", nwrite);
    } else {
        printf("Wrote %d bytes\n", nwrite);
    }

    return 0;
}

static void prstat(const char *name) {
    /* print out stat buf */
    printf("%c%c%c%c 0x%06x 0x%lx 0x%06lx %s\n",
            sbuf.st_type == ST_SPECIAL ? 's' : '-',
            sbuf.st_fmode & FM_READ ? 'r' : '-',
            sbuf.st_fmode & FM_WRITE ? 'w' : '-',
            sbuf.st_fmode & FM_EXEC ? 'x' : '-', sbuf.st_size, sbuf.st_ctime,
            sbuf.st_atime, name);
}

static int cat(int argc, char **argv) {
    int fd;
    char buf[BUF_SIZ];
    int num_read, stdout_fd, num_written = 0;


    if (argc != 2) {
        printf("Usage: cat filename\n");
        return 1;
    }

    printf("<%s>\n", argv[1]);

    fd = open(argv[1], O_RDONLY);
    stdout_fd = open("console", O_WRONLY);

    assert(fd >= 0);

    while ((num_read = read(fd, buf, BUF_SIZ)) > 0)
        num_written = write(stdout_fd, buf, num_read);

    close(stdout_fd);

    if (num_read == -1 || num_written == -1) {
        printf("error on write %d %d\n", num_read, num_written);
        return 1;
    }

    return 0;
}

static int cp(int argc, char **argv) {
    int fd, fd_out;
    char *file1, *file2;
    char buf[BUF_SIZ];
    int num_read, num_written = 0;

    if (argc != 3) {
        printf("Usage: cp from to\n");
        return 1;
    }

    file1 = argv[1];
    file2 = argv[2];

    fd = open(file1, O_RDONLY);
    fd_out = open(file2, O_WRONLY);

    assert(fd >= 0);

    while ((num_read = read(fd, buf, BUF_SIZ)) > 0)
        num_written = write(fd_out, buf, num_read);

    if (num_read == -1 || num_written == -1) {
        printf("error on cp\n");
        return 1;
    }

    return 0;
}

#define MAX_PROCESSES 10

static int ps(int argc, char **argv) {
    sos_process_t *process;
    int i, processes;

    process = malloc(MAX_PROCESSES * sizeof(*process));

    if (process == NULL) {
        printf("%s: out of memory\n", argv[0]);
        return 1;
    }

    processes = sos_process_status(process, MAX_PROCESSES);

    printf("TID SIZE   STIME   CTIME COMMAND\n");

    for (i = 0; i < processes; i++) {
        printf("%3x %4x %7d %s\n", process[i].pid, process[i].size,
                process[i].stime, process[i].command);
    }

    free(process);

    return 0;
}

static int exec(int argc, char **argv) {
    pid_t pid;
    int r;
    int bg = 0;

    if (argc < 2 || (argc > 2 && argv[2][0] != '&')) {
        printf("Usage: exec filename [&]\n");
        return 1;
    }

    if ((argc > 2) && (argv[2][0] == '&')) {
        bg = 1;
    }

    if (bg == 0) {
        r = close(in);
        assert(r == 0);
    }

    pid = sos_process_create(argv[1]);
    if (pid >= 0) {
        printf("Child pid=%d\n", pid);
        if (bg == 0) {
            sos_process_wait(pid);
        }
    } else {
        printf("Failed!\n");
    }
    if (bg == 0) {
        in = open("console", O_RDONLY);
        assert(in >= 0);
    }
    return 0;
}

static int dir(int argc, char **argv) {
    int i = 0, r;
    char buf[BUF_SIZ];

    if (argc > 2) {
        printf("usage: %s [file]\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        r = sos_stat(argv[1], &sbuf);
        if (r < 0) {
            printf("stat(%s) failed: %d\n", argv[1], r);
            return 0;
        }
        prstat(argv[1]);
        return 0;
    }

    while (1) {
        r = sos_getdirent(i, buf, BUF_SIZ);
        if (r < 0) {
            printf("dirent(%d) failed: %d\n", i, r);
            break;
        } else if (!r) {
            break;
        }
        r = sos_stat(buf, &sbuf);
        if (r < 0) {
            printf("stat(%s) failed: %d\n", buf, r);
            break;
        }
        prstat(buf);
        i++;
    }
    return 0;
}

static int second_sleep(int argc,char *argv[]) {
    if (argc != 2) {
        printf("Usage %s seconds\n", argv[0]);
        return 1;
    }
    sleep(atoi(argv[1]));
    return 0;
}

static int milli_sleep(int argc,char *argv[]) {
    struct timespec tv;
    uint64_t nanos;
    if (argc != 2) {
        printf("Usage %s milliseconds\n", argv[0]);
        return 1;
    }
    nanos = (uint64_t)atoi(argv[1]) * NS_IN_MS;
    /* Get whole seconds */
    tv.tv_sec = nanos / NS_IN_S;
    /* Get nanos remaining */
    tv.tv_nsec = nanos % NS_IN_S;
    nanosleep(&tv, NULL);
    return 0;
}

static int second_time(int argc, char *argv[]) {
    printf("%d seconds since boot\n", (int)time(NULL));
    return 0;
}

static int micro_time(int argc, char *argv[]) {
    struct timeval time;
    gettimeofday(&time, NULL);
    uint64_t micros = (uint64_t)time.tv_sec * US_IN_S + (uint64_t)time.tv_usec;
    printf("%llu microseconds since boot\n", micros);
    return 0;
}

struct command {
    char *name;
    int (*command)(int argc, char **argv);
};

struct command commands[] = { { "dir", dir }, { "ls", dir }, { "cat", cat }, {
        "cp", cp }, { "ps", ps }, { "exec", exec }, {"sleep",second_sleep}, {"msleep",milli_sleep},
        {"time", second_time}, {"mtime", micro_time}, {"open", sosh_open}, {"read", sosh_read}, {"write", sosh_write} };

static uint64_t mtime(void) {
    struct timeval time;
    gettimeofday(&time, NULL);
    uint64_t micros = (uint64_t)time.tv_sec * US_IN_S + (uint64_t)time.tv_usec;
    return micros;
}

#define MAX_BUF_SIZE 4096*16

void touch_pages(char *buf, size_t num_bytes) {
    for (size_t i = 0; i < num_bytes; i += 4096) {
        buf[i] = (char)i;
        if (buf[i] != (char)i) {
            printf("failed touching pages %c %c\n", buf[i], (char)i);
            while (true);
        }
    }
}

int read_res_fd, write_res_fd;

void mapped_read_benchmark(size_t buf_size, int num_runs) {
    //printf("Benchmark buf_size = %u, num_runs = %d\n", buf_size, num_runs);
    char buf[MAX_BUF_SIZE];
    uint64_t before_cache[num_runs+1];
    uint64_t after_cache[num_runs+1];
    touch_pages(buf, buf_size);

    for (int i = 0; i < num_runs; ++i) {
        for (int j = 0; j < buf_size; ++j) {
            buf[j] = 0;
        }
        int fd = open("read_test", O_RDWR);
        before_cache[i] = mtime();
        int num_read = read(fd, buf, buf_size);
        after_cache[i] = mtime();
        for (int j = 0; j < buf_size; ++j) {
            if (buf[j] != 'a') {
                while(true);
            }
            assert(buf[j] == 'a');
        }
        if (num_read != buf_size) while (true);
        assert(num_read == buf_size);
        close(fd);
    }

    write(read_res_fd, "\n", 1);
    for (int i = 0; i < num_runs; ++i) {
        char buf[20];
        sprintf(buf, "%llu\n", after_cache[i] - before_cache[i]);
        int len = strlen(buf);
        write(read_res_fd, buf, len);
    }
}

void mapped_write_benchmark(size_t buf_size, int num_runs) {
    //printf("Benchmark buf_size = %u, num_runs = %d\n", buf_size, num_runs);
    char buf[MAX_BUF_SIZE];
    uint64_t before_cache[num_runs+1];
    uint64_t after_cache[num_runs+1];
    touch_pages(buf, buf_size);

    for (int i = 0; i < num_runs; ++i) {
        for (int j = 0; j < buf_size; ++j) {
            buf[j] = (char)(j % 26 + 'a');
        }
        int fd = open("write_test", O_RDWR);
        before_cache[i] = mtime();
        int num_write = write(fd, buf, buf_size);
        after_cache[i] = mtime();

        if (num_write != buf_size) while (true);
        assert(num_write == buf_size);

        int fd_read = open("write_test", O_RDWR);
        for (int j = 0; j < buf_size; ++j) {
            buf[j] = 0;
        }
        assert(read(fd_read, buf, buf_size) == num_write);
        for (int j = 0; j < buf_size; ++j) {
            if (buf[j] != ((char)(j % 26 + 'a'))) {
                printf("write assertion failed\n");
                while (true);
            }
        }
        close(fd_read);

        close(fd);
    }

    write(write_res_fd, "\n", 1);
    for (int i = 0; i < num_runs; ++i) {
        char buf[20];
        sprintf(buf, "%llu\n", after_cache[i] - before_cache[i]);
        int len = strlen(buf);
        write(write_res_fd, buf, len);
    }
}

void open_close_benchmark(int num_opens) {
    char buf[num_opens][5];
    for (int i = 0; i < num_opens; ++i) {
        sprintf(buf[i], "%d", i);
    }
    int fds[num_opens];

    uint64_t t1 = mtime(); 
    fds[0] = open("0", O_RDWR);
    for (int i = 1; i < num_opens; ++i) {
        fds[i] = open(buf[i], O_RDWR);
    }
    uint64_t t2 = mtime();
    
    for (int i = 0; i < num_opens; ++i) {
        close(fds[i]);
    }

    uint64_t t3 = mtime();

    printf("%llu %llu\n", t2-t1, t3-t2);
}

//#define NPAGES (4*1024)
#define NPAGES 64
/* called from pt_test */
static void
do_pt_test( char *buf )
{
    int i;

    /* set */
    for(i = 0; i < NPAGES; i ++) {
        for (int j = 0; j < 4096; ++j) {
	        buf[i * 4096 + j] = i + j;
        }
    }

    //int a = 0;
    //for (i = 0; i < (int)1e9; ++i) {
    //    a += i;
    //}

    //printf("Done touching pages. Checking now\n");
    //sleep(5);
    //printf("%d\n", a);
    

    /* check */
    for(i = 0; i < NPAGES; i ++) {
        for (int j = 0; j < 4096; ++j) {
	        assert(buf[i * 4096 + j] == (char)(i + j));
        }
    }
}

static void
pt_test( void )
{
    /* need a decent sized stack */
    char buf1[NPAGES * 4096], *buf2 = NULL;

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) 0x20000000);

    /* stack test */
    do_pt_test(buf1);
    printf("Stack test ok\n");

    /* heap test */
    buf2 = malloc(100000);
    printf("%p\n", buf2);
    assert(buf2);
    do_pt_test(buf2);
    free(buf2);

    buf2 = malloc(1500000);
    printf("%p\n", buf2);
    assert(buf2);
    do_pt_test(buf2);

    char *buf3 = malloc(100000);
    do_pt_test(buf3);
    printf("%p\n", buf3);
    free(buf3);
    free(buf2);
    printf("Heap test ok\n");

}
int main(void) {
    /* Testing mapped pages */

//    read_res_fd = open("readres", O_RDWR);
//    write_res_fd = open("writeres", O_RDWR);

//    for (int i = 1; i <= 4096*16; i <<= 1) {
//        mapped_read_benchmark(i, 16);
//    }
//    for (int i = 1; i <= 4096*16; i <<= 1) {
//        mapped_write_benchmark(i, 16);
//    }

//    double sum = 0;
//    for (int i = 0; i < 16; ++i) {
//        uint64_t t1 = mtime();
//        uint64_t t2 = mtime();
//
//        sum += t2 - t1;
//    }
//    printf("mtime avg %lf\n", sum / 16.0);
//
//    for (int i = 0; i < 16; ++i) {
//        open_close_benchmark(25);
//    }
//
//    return 0;
    pt_test();
    printf("IT WORKED\n");

    char buf[BUF_SIZ];
    char *argv[MAX_ARGS];
    int i, r, done, found, new, argc;
    char *bp, *p;

    // sos_stat_t buf2;
    // int err = sos_stat("bootimg.elf", &buf2);
    // assert(!err);

    // printf("%d %d %d %ld %ld\n", buf2.st_type, buf2.st_fmode, buf2.st_size, buf2.st_ctime, buf2.st_atime);

    in = open("console", O_RDWR);
    //in = 0;
    assert(in >= 0);

    bp = buf;
    done = 0;
    new = 1;
    
//    char *buf2 = malloc(2*4096);
//    read(in, buf2 + 4096 + 30, 4096);
//
//    printf("buf2 = %s\n", buf2 + 4096 + 30);

    printf("\n[SOS Starting]\n");


    while (!done) {
        if (new) {
            printf("$ ");
        }
        new = 0;
        found = 0;

        while (!found && !done) {
            /* Make sure to flush so anything is visible while waiting for user input */
            fflush(stdout);
            r = read(in, bp, BUF_SIZ - 1 + buf - bp);
            if (r < 0) {
                printf("Console read failed!\n");
                done = 1;
                break;
            }
            bp[r] = 0; /* terminate */
            for (p = bp; p < bp + r; p++) {
                if (*p == '\03') { /* ^C */
                    printf("^C\n");
                    p = buf;
                    new = 1;
                    break;
                } else if (*p == '\04') { /* ^D */
                    p++;
                    found = 1;
                } else if (*p == '\010' || *p == 127) {
                    /* ^H and BS and DEL */
                    if (p > buf) {
                        printf("\010 \010");
                        p--;
                        r--;
                    }
                    p--;
                    r--;
                } else if (*p == '\n') { /* ^J */
                    printf("%c", *p);
                    *p = 0;
                    found = p > buf;
                    p = buf;
                    new = 1;
                    break;
                } else {
                    printf("%c", *p);
                }
            }
            bp = p;
            if (bp == buf) {
                break;
            }
        }

        if (!found) {
            continue;
        }

        argc = 0;
        p = buf;

        while (*p != '\0') {
            /* Remove any leading spaces */
            while (*p == ' ')
                p++;
            if (*p == '\0')
                break;
            argv[argc++] = p; /* Start of the arg */
            while (*p != ' ' && *p != '\0') {
                p++;
            }

            if (*p == '\0')
                break;

            /* Null out first space */
            *p = '\0';
            p++;
        }

        if (argc == 0) {
            continue;
        }

        found = 0;

        for (i = 0; i < sizeof(commands) / sizeof(struct command); i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].command(argc, argv);
                found = 1;
                break;
            }
        }

        /* Didn't find a command */
        if (found == 0) {
            /* They might try to exec a program */
            if (sos_stat(argv[0], &sbuf) != 0) {
                printf("Command \"%s\" not found\n", argv[0]);
            } else if (!(sbuf.st_fmode & FM_EXEC)) {
                printf("File \"%s\" not executable\n", argv[0]);
            } else {
                /* Execute the program */
                argc = 2;
                argv[1] = argv[0];
                argv[0] = "exec";
                exec(argc, argv);
            }
        }
    }
    printf("[SOS Exiting]\n");
}
