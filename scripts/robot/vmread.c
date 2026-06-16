/* Passive read of a UNIX stream socket for a fixed duration (no commands sent unless argv[3]). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/tmp/videomonitor.socket";
    const char *out  = argc > 2 ? argv[2] : "/tmp/vm.bin";
    int dur = argc > 3 ? atoi(argv[3]) : 12;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
    strncpy(a.sun_path, path, sizeof a.sun_path - 1);
    if (connect(fd, (void *)&a, sizeof a) < 0) { perror("connect"); return 1; }
    struct timeval tv = { 1, 0 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    FILE *f = fopen(out, "wb");
    char buf[65536]; long total = 0; int n; time_t start = time(0);
    while (time(0) - start < dur && total < 8000000) {
        n = read(fd, buf, sizeof buf);
        if (n > 0) { fwrite(buf, 1, n, f); total += n; }
        else if (n == 0) break;        /* peer closed */
        /* n<0 (timeout) -> keep waiting */
    }
    fclose(f);
    printf("read %ld bytes over <=%ds\n", total, dur);
    return 0;
}
