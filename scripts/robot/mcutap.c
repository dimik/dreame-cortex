/* ============================================================================================
 * mcutap.c — LD_PRELOAD read-tap for the MCU sensor stream (Dreame D10s Pro, /dev/ttyS4).
 *
 * STATUS — read() interposition IS VIABLE with the errno fix below (verified 2026-06-19: AVA runs
 * normally with an errno-correct read() interposer mapped in; `accel_z=1.000g`/odom decode confirmed
 * earlier via strace). The original mcutap broke AVA NOT because interposing read() is unsafe, but
 * because the freestanding read() returned the raw syscall value (`-errno`) instead of glibc's
 * contract (return -1 and set errno) — AVA's non-blocking read/select loops saw e.g. -11 instead of
 * -1/EAGAIN and choked. Fixed here via __errno_location (resolved from the host glibc at load).
 *
 * REMAINING for a production tap: the per-frame `sendto` below is a syscall in AVA's hot read path
 * — replace with a shm-ring tee (memcpy, no syscall) before preloading for real. A LiDAR variant
 * keys on the LDS frame marker 0x55 0xaa on /dev/ttyS3 instead of 0x3c on ttyS4. This unblocks both
 * the raw-IMU and raw-/scan taps (see docs/ros.md, docs/sensors.md).
 * ============================================================================================
 * AVA reads the MCU Status stream (IMU gyro/accel, wheel odometry, motor currents, triggers)
 * from /dev/ttyS4. We can't open that port ourselves (AVA holds it exclusively), so this shim
 * is LD_PRELOAD'd into AVA and tees the bytes AVA already read — purely read-only, AVA untouched.
 *
 * Each MCU frame starts with 0x3c ('<') ... 0x3e ('>') (see fanoff_shim.c). AVA's reads are
 * frame-aligned, so we forward any read() buffer that begins with 0x3c to a unix DGRAM socket
 * (/tmp/mcu_tap.sock). A ROS node binds that socket, re-frames, decodes (Status10ms=IMU,
 * Status20ms=odom, ...), calibrates, and publishes /imu/data + /odom.
 *
 * DESIGN NOTES
 *  - Hooks read()/readv() only — symbols NOT claimed by fanoff (write/writev) or camsiphon
 *    (open/openat/mmap/ioctl), so the three shims compose with no interposition collision. (We
 *    deliberately do NOT hook openat to track the fd, which camsiphon already owns — the 0x3c
 *    heuristic identifies MCU frames without it. The ROS decoder validates frames by CRC.)
 *  - DGRAM + MSG_NOSIGNAL, NOT a FIFO: a FIFO whose reader has gone raises SIGPIPE on write,
 *    which would terminate AVA. sendto on a connectionless unix DGRAM just drops (ECONNREFUSED)
 *    when no ROS node is bound → zero overhead when nobody's listening, and never signals AVA.
 *  - Freestanding (-nostdlib), raw aarch64 syscalls. Built/loaded alongside the other shims;
 *    see build_ava_shims.sh.
 * ============================================================================================ */
#include <stddef.h>

#define SYS_read    63
#define SYS_readv   65
#define SYS_socket  198
#define SYS_sendto  206
#define AF_UNIX      1
#define SOCK_DGRAM   2
#define MSG_NOSIGNAL 0x4000

struct iovec { void *iov_base; size_t iov_len; };

/* glibc errno contract: on error, return -1 and set errno — NOT the raw -errno. AVA's non-blocking
 * read/select loops choke on a raw -errno (they expect -1/EAGAIN). __errno_location resolves from
 * the host glibc at load (thread-local errno). This was the bug that broke the original tap. */
extern int *__errno_location(void);
static long ret(long r){ if (r < 0) { *__errno_location() = (int)(-r); return -1; } return r; }

static long sys3(long n,long a,long b,long c){register long x8 asm("x8")=n,x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c;asm volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2):"memory","cc");return x0;}
static long sys6(long n,long a,long b,long c,long d,long e,long f){register long x8 asm("x8")=n,x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c,x3 asm("x3")=d,x4 asm("x4")=e,x5 asm("x5")=f;asm volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5):"memory","cc");return x0;}

/* sockaddr_un for /tmp/mcu_tap.sock, built once */
static int  g_sock = -1;
static unsigned char g_addr[110];   /* [0..1]=AF_UNIX, [2..]=path\0 */
static int  g_addrlen = 0;

static void tap_init(void) {
    g_sock = (int)sys3(SYS_socket, AF_UNIX, SOCK_DGRAM, 0);
    if (g_sock < 0) return;
    g_addr[0] = AF_UNIX; g_addr[1] = 0;
    static const char p[] = "/tmp/mcu_tap.sock";
    int i = 0; for (; p[i]; i++) g_addr[2 + i] = (unsigned char)p[i];
    g_addr[2 + i] = 0;
    g_addrlen = 2 + i + 1;
}

/* forward a buffer to the ROS node; dropped (no signal) if nobody is bound */
static void tee(const void *buf, long n) {
    if (g_sock < 0) { tap_init(); if (g_sock < 0) return; }
    sys6(SYS_sendto, g_sock, (long)buf, n, MSG_NOSIGNAL, (long)g_addr, g_addrlen);
}

long read(int fd, void *buf, size_t count) {
    long r = sys3(SYS_read, fd, (long)buf, (long)count);
    if (r >= 4 && ((const unsigned char *)buf)[0] == 0x3c) tee(buf, r);
    return ret(r);
}

long readv(int fd, const struct iovec *iov, int iovcnt) {
    long r = sys3(SYS_readv, fd, (long)iov, (long)iovcnt);
    if (r >= 4 && iovcnt > 0 && iov && iov[0].iov_len >= 1 &&
        ((const unsigned char *)iov[0].iov_base)[0] == 0x3c) {
        long left = r;
        for (int i = 0; i < iovcnt && left > 0; i++) {
            long m = (long)iov[i].iov_len; if (m > left) m = left;
            tee(iov[i].iov_base, m); left -= m;
        }
    }
    return ret(r);
}
