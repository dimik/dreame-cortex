/* nanomsg SP PAIR raw client over a UNIX/IPC stream socket.
 * Handshake: 8-byte greeting "\0SP\0" + protocol(2 BE)=0x0010(PAIR) + reserved(2).
 * Then SP stream framing: each message = [uint64 BE size][payload]. We dump raw + parse. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
static int rd(int fd, void *b, int n){ int g=0; while(g<n){ int r=read(fd,(char*)b+g,n-g); if(r<=0) return g?g:r; g+=r;} return g; }
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/tmp/videomonitor.socket";
    const char *out  = argc > 2 ? argv[2] : "/tmp/vm.bin";
    int dur = argc > 3 ? atoi(argv[3]) : 10;
    const char *sendmsg = argc > 4 ? argv[4] : NULL;   /* optional: a message to send after handshake */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a; memset(&a,0,sizeof a); a.sun_family=AF_UNIX; strncpy(a.sun_path,path,sizeof a.sun_path-1);
    if (connect(fd,(void*)&a,sizeof a)<0){ perror("connect"); return 1; }
    unsigned char greet[8] = {0x00,0x53,0x50,0x00,0x00,0x10,0x00,0x00};
    write(fd, greet, 8);
    unsigned char peer[8]; int g = rd(fd, peer, 8);
    printf("peer greeting (%d): ", g); for(int i=0;i<g;i++) printf("%02x ", peer[i]); printf("\n");
    struct timeval tv={1,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
    if (sendmsg) {
        uint64_t L = strlen(sendmsg); unsigned char hdr[8];
        for(int i=0;i<8;i++) hdr[i] = (L >> (8*(7-i))) & 0xff;       /* BE size */
        write(fd, hdr, 8); write(fd, sendmsg, L);
        printf("sent msg (%llu bytes): %s\n", (unsigned long long)L, sendmsg);
    }
    FILE *f=fopen(out,"wb"); long total=0; time_t start=time(0); int msgs=0;
    while (time(0)-start < dur) {
        unsigned char hdr[8]; int h=rd(fd,hdr,8);
        if (h==0) { printf("peer closed\n"); break; }
        if (h<8) continue;                                /* timeout/partial */
        uint64_t L=0; for(int i=0;i<8;i++) L=(L<<8)|hdr[i];
        if (L==0 || L>4000000) { printf("odd msg len %llu (hdr ", (unsigned long long)L); for(int i=0;i<8;i++)printf("%02x",hdr[i]); printf(")\n"); continue; }
        unsigned char *p=malloc(L); int got=rd(fd,p,L);
        fwrite(p,1,got,f); total+=got; msgs++;
        printf("MSG #%d len=%llu first: ", msgs, (unsigned long long)L);
        for(int i=0;i<got&&i<24;i++) printf("%02x ", p[i]); 
        printf(" | ascii: "); for(int i=0;i<got&&i<48;i++){ char c=p[i]; putchar(c>=32&&c<127?c:'.'); }
        printf("\n"); free(p);
    }
    fclose(f); printf("total %ld bytes in %d msgs -> %s\n", total, msgs, out);
    return 0;
}
