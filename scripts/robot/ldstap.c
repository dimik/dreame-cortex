/* ============================================================================================
 * ldstap.c — LD_PRELOAD read-tap for AVA's LiDAR serial (Dreame D10s Pro, /dev/ttyS3 @ 230400).
 * ============================================================================================
 * AVA owns /dev/ttyS3 exclusively (node_lds.so -> SLAM). We can't open it ourselves, so this shim
 * is LD_PRELOAD'd into AVA and tees the bytes AVA ALREADY read — purely read-only, AVA's flow is
 * untouched (we call the real read(), then copy the returned bytes out). A ROS node decodes the
 * LDS frames (see scripts/robot/lds_decode.py) and publishes sensor_msgs/LaserScan on /scan.
 *
 * WHY THIS IS AVA-SAFE (the hard-won lessons):
 *  - errno contract: a freestanding read() MUST return -1 and set errno on error, NOT the raw
 *    -errno. AVA's non-blocking read/select loops choke on a raw -errno (e.g. -11 vs -1/EAGAIN).
 *    This was the bug that broke the first read-tap (mcutap). Fixed here via __errno_location
 *    (resolved from AVA's glibc 2.23 at load). Verified: AVA runs normally with this mapped in.
 *  - shm ring, not sendto: we memcpy into a RAM-backed ring (/tmp/lds_ring.buf, tmpfs) with NO
 *    syscall in AVA's hot read path (the ring is mmap'd once). mcutap's per-frame sendto was a
 *    syscall on every frame; this removes it.
 *  - fd isolation: the LiDAR stream is fragmented (1-byte 0x55, 1-byte 0xaa, then the body), and
 *    other threads read other fds concurrently, so we CANNOT key on buffer content (the 55/aa
 *    would interleave with MCU bytes in the ring). We tee ONLY reads on the ttyS3 fd, found once
 *    by scanning /proc/self/fd (robust to fd-number changes; no openat hook -> no clash with
 *    camsiphon). Zero overhead once found, and zero when the turret is gated off (no ttyS3 reads).
 *
 * Exports read()/readv() only — not claimed by fanoff (write/writev) or camsiphon (open/openat/
 * mmap/ioctl), so the three shims compose with no interposition collision.
 *
 * Freestanding (-nostdlib): raw aarch64 svc syscalls, built in the glibc-2.39 chroot, loadable
 * under AVA's glibc 2.23. See build_ava_shims.sh.
 * ============================================================================================ */
#include <stddef.h>

#define SYS_read       63
#define SYS_readv      65
#define SYS_openat     56
#define SYS_close      57
#define SYS_ftruncate  46
#define SYS_mmap      222
#define SYS_readlinkat 78
#define AT_FDCWD      (-100)

struct iovec { void *iov_base; size_t iov_len; };

static long sys1(long n,long a){register long x8 asm("x8")=n,x0 asm("x0")=a;asm volatile("svc #0":"+r"(x0):"r"(x8):"memory","cc");return x0;}
static long sys3(long n,long a,long b,long c){register long x8 asm("x8")=n,x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c;asm volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2):"memory","cc");return x0;}
static long sys4(long n,long a,long b,long c,long d){register long x8 asm("x8")=n,x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c,x3 asm("x3")=d;asm volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3):"memory","cc");return x0;}
static long sys6(long n,long a,long b,long c,long d,long e,long f){register long x8 asm("x8")=n,x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c,x3 asm("x3")=d,x4 asm("x4")=e,x5 asm("x5")=f;asm volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5):"memory","cc");return x0;}

/* glibc errno contract — return -1 + errno on error (NOT raw -errno). The bug that broke mcutap. */
extern int *__errno_location(void);
static long ret(long r){ if (r < 0) { *__errno_location() = (int)(-r); return -1; } return r; }

static int n2s(unsigned v, char *o){ char t[12]; int i=0,j=0; if(!v){o[0]='0';return 1;} while(v){t[i++]='0'+v%10;v/=10;} while(i)o[j++]=t[--i]; return j; }

/* ---- locate the ttyS3 fd by reading /proc/self/fd/<n> symlinks (once, robust to fd renumbering) */
static int g_lds_fd = -1;
static unsigned g_scan_ctr = 0;

static int is_ttyS3(int fd) {
    char path[24] = "/proc/self/fd/";          /* 14 chars */
    int p = 14; p += n2s((unsigned)fd, path + p); path[p] = 0;
    char buf[40];
    long len = sys4(SYS_readlinkat, AT_FDCWD, (long)path, (long)buf, sizeof buf);
    static const char t[] = "/dev/ttyS3";       /* 10 chars */
    if (len != 10) return 0;
    for (int i = 0; i < 10; i++) if (buf[i] != t[i]) return 0;
    return 1;
}
static void find_lds(void) {
    for (int fd = 3; fd < 64; fd++) if (is_ttyS3(fd)) { g_lds_fd = fd; return; }
}

/* ---- lock-free SPSC ring in tmpfs, shared with the ROS decoder ---- */
#define RING (256UL*1024)
#define HDR  64UL                               /* hdr[0]=write_pos hdr[1]=magic hdr[2]=ringsize */
#define MAGIC 0x0031534444530001UL              /* "LDS" + version */
static unsigned long g_ring = 0;

static void fast_copy(unsigned char *d, const unsigned char *s, unsigned long n){
    unsigned long i=0; for(;i+8<=n;i+=8)*(unsigned long*)(d+i)=*(const unsigned long*)(s+i); for(;i<n;i++)d[i]=s[i];
}
static void ring_map(void){
    int fd=(int)sys4(SYS_openat,AT_FDCWD,(long)"/tmp/lds_ring.buf",0x42/*O_RDWR|O_CREAT*/,0644);
    if(fd<0) return;
    sys3(SYS_ftruncate,fd,HDR+RING,0);
    long r=sys6(SYS_mmap,0,HDR+RING,3/*RW*/,1/*MAP_SHARED*/,fd,0);
    sys1(SYS_close,fd);
    if(r>0){ g_ring=(unsigned long)r;
        volatile unsigned long *h=(volatile unsigned long*)g_ring;
        h[1]=MAGIC; h[2]=RING;                  /* leave h[0]=write_pos as-is (continue across reopen) */
    }
}
static void ring_put(const void *buf, long n){
    if(n<=0) return; if((unsigned long)n>RING) n=RING;
    if(!g_ring){ ring_map(); if(!g_ring) return; }
    volatile unsigned long *h=(volatile unsigned long*)g_ring;
    unsigned char *data=(unsigned char*)(g_ring+HDR);
    unsigned long wp=h[0], off=wp%RING; long first=n;
    if(off+(unsigned long)first>RING) first=(long)(RING-off);
    fast_copy(data+off,(const unsigned char*)buf,first);
    if(n>first) fast_copy(data,(const unsigned char*)buf+first,n-first);
    asm volatile("dmb ish":::"memory");         /* publish data before advancing write_pos */
    h[0]=wp+n;
}

/* ---- interposed entry points ---- */
long read(int fd, void *buf, size_t count) {
    long r = sys3(SYS_read, fd, (long)buf, (long)count);
    if (g_lds_fd < 0 && (g_scan_ctr++ & 0x3FF) == 0) find_lds();   /* scan until found, then never */
    if (fd == g_lds_fd && r > 0) ring_put(buf, r);
    return ret(r);
}

long readv(int fd, const struct iovec *iov, int iovcnt) {
    long r = sys3(SYS_readv, fd, (long)iov, (long)iovcnt);
    if (g_lds_fd < 0 && (g_scan_ctr++ & 0x3FF) == 0) find_lds();
    if (fd == g_lds_fd && r > 0 && iov) {
        long left = r;
        for (int i = 0; i < iovcnt && left > 0; i++) {
            long m = (long)iov[i].iov_len; if (m > left) m = left;
            ring_put(iov[i].iov_base, m); left -= m;
        }
    }
    return ret(r);
}
