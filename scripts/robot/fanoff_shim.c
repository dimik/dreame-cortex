/*
 * fanoff_shim.c — LD_PRELOAD interposer on AVA's MCU serial link (/dev/ttyS4, fd varies).
 *
 * GOAL: keep the vacuum fan OFF in every mode (manual nav, cleaning, etc.) by rewriting the
 * fan-level byte of SetCleaning packets AVA writes to the MCU. This is the single chokepoint
 * downstream of all of AVA's CleanMode/fan_speed/work_mode logic.
 *
 * PROTOCOL (reverse-engineered 2026-06-15, cross-checked w/ github.com/alufers/dreame_mcu_protocol):
 *   Channel : /dev/ttyS4 (NOT ttyS3). Frames written via write(). fd is not fixed across boots,
 *             so we detect frames by the 0x3c start byte, not by fd number.
 *   Framing : 3c <len> <type> <payload[len]> <crc_hi> <crc_lo> 3e
 *             '?' (0x3f) escapes a literal 0x3c/0x3e/0x3f inside the frame body.
 *   CRC     : Modbus CRC16 (table below) over the UNescaped bytes [len, type, payload...],
 *             stored big-endian (hi, lo) just before 0x3e.
 *   Fan cmd : type 0x01 = "SetCleaning" (controls fan/brush/pump). 5-byte payload f1..f5.
 *             Live experiment (sweeping the Valetudo fan preset in manual nav) showed ONLY
 *             payload[2] (f3) tracks fan speed: low/med=0x03, max=0x05, off(docked)=0x00.
 *             f1(0x55)/f2(0x58) are brush/pump (constant when cleaning active) and are LEFT
 *             UNTOUCHED. => zero payload[2] to silence the vacuum fan only.
 *
 *   Verified frames (CRC reproduced exactly by the table below):
 *     low/med : 3c 05 01 55 58 03 00 00 bd a0 3e
 *     max     : 3c 05 01 55 58 05 00 00 bc 40 3e
 *     FILTERED: 3c 05 01 55 58 00 00 00 bd 50 3e   <- what this shim emits (f3=0)
 *
 * FREESTANDING (-nostdlib): AVA links glibc 2.23 but we build in a glibc-2.39 chroot, so we
 * use NO libc — raw svc #0 syscalls + hand-rolled helpers. Exports write()/writev().
 *
 * Build: see build_fanoff.sh  (MODE_FILTER = production, MODE_LOG = passthrough+dump).
 */

#include <stddef.h>

#define SYS_write  64
#define SYS_writev 66
#define SYS_openat 56
#define AT_FDCWD   (-100)

struct iovec { void *iov_base; size_t iov_len; };

static long sys3(long n, long a, long b, long c) {
    register long x8 asm("x8") = n, x0 asm("x0") = a, x1 asm("x1") = b, x2 asm("x2") = c;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory", "cc");
    return x0;
}
static long sys4(long n, long a, long b, long c, long d) {
    register long x8 asm("x8") = n, x0 asm("x0") = a, x1 asm("x1") = b, x2 asm("x2") = c, x3 asm("x3") = d;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
    return x0;
}
static long raw_write(int fd, const void *buf, unsigned long n) { return sys3(SYS_write, fd, (long)buf, (long)n); }

/* ---- Modbus CRC16 (ported from dreame_mcu_protocol/crc_util.py g_McRctable_16) ---- */
static const unsigned short CRC_TAB[256] = {
0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040,
};
static unsigned short crc16(const unsigned char *p, unsigned long n) {
    unsigned short crc = 0xFFFF;
    for (unsigned long i = 0; i < n; i++)
        crc = (crc >> 8) ^ CRC_TAB[(crc ^ p[i]) & 0xFF];
    return crc;
}

#define FRAME_START 0x3c
#define FRAME_END   0x3e
#define FRAME_ESC   0x3f
#define TYPE_SETCLEANING 0x01
#define FAN_PAYLOAD_IDX  2          /* f3 within the 5-byte SetCleaning payload */

static int is_special(unsigned char b) { return b == FRAME_START || b == FRAME_END || b == FRAME_ESC; }

#ifdef MODE_LOG
static int log_fd = -2;
static void log_bytes(const unsigned char *b, unsigned long n) {
    if (log_fd == -2) { static const char p[] = "/tmp/mcu_tx.log";
        log_fd = (int)sys4(SYS_openat, AT_FDCWD, (long)p, 1 | 0100 | 02000, 0644); }
    if (log_fd >= 0) { unsigned char sep[2] = {0xFF,0xFF}; raw_write(log_fd, sep, 2); raw_write(log_fd, b, n); }
}
#endif

/*
 * Scan `in` for 3c..3e frames. For type-0x01 (SetCleaning) frames, zero payload[2] (fan) and
 * recompute the CRC; re-escape and emit. Everything else (other frames, raw bytes) is copied
 * verbatim. Returns number of bytes written into `out`.
 */
static unsigned long process(const unsigned char *in, unsigned long n, unsigned char *out) {
    unsigned long i = 0, o = 0;
    while (i < n) {
        if (in[i] != FRAME_START) { out[o++] = in[i++]; continue; }
        /* unescape frame body into content[] until an unescaped 0x3e */
        unsigned char content[64]; unsigned long clen = 0, j = i + 1; int closed = 0;
        while (j < n && clen < sizeof(content)) {
            unsigned char c = in[j++];
            if (c == FRAME_END) { closed = 1; break; }
            if (c == FRAME_ESC) { if (j < n) content[clen++] = in[j++]; }
            else content[clen++] = c;
        }
        if (!closed) { while (i < n) out[o++] = in[i++]; break; }  /* no end in buffer: pass rest */
#ifdef MODE_LOG
        log_bytes(in + i, j - i);
#endif
        /* content = [len][type][payload(len)][crc_hi][crc_lo] ; total = len+4 */
        unsigned int len = content[0];
        int is_setcleaning = (clen == len + 4u) && (content[1] == TYPE_SETCLEANING)
                             && (len > FAN_PAYLOAD_IDX);
#ifdef MODE_FILTER
        if (is_setcleaning && content[2 + FAN_PAYLOAD_IDX] != 0) {
            content[2 + FAN_PAYLOAD_IDX] = 0;                 /* fan level -> off */
            unsigned short crc = crc16(content, 2 + len);     /* over len+type+payload */
            content[2 + len]     = (unsigned char)(crc >> 8); /* big-endian trailer */
            content[2 + len + 1] = (unsigned char)(crc & 0xFF);
            out[o++] = FRAME_START;
            for (unsigned long k = 0; k < clen; k++) {
                if (is_special(content[k])) out[o++] = FRAME_ESC;
                out[o++] = content[k];
            }
            out[o++] = FRAME_END;
            i = j;
            continue;
        }
#else
        (void)is_setcleaning; (void)len;
#endif
        while (i < j) out[o++] = in[i++];   /* copy frame verbatim */
    }
    return o;
}

#define MAXIN 1024
long write(int fd, const void *buf, size_t count) {
    const unsigned char *p = (const unsigned char *)buf;
    if (count >= 4 && count <= MAXIN && p[0] == FRAME_START) {
        unsigned char out[2 * MAXIN];
        unsigned long o = process(p, count, out);
        raw_write(fd, out, o);
        return (long)count;   /* tell AVA all its bytes went out */
    }
    return raw_write(fd, buf, count);
}

long writev(int fd, const struct iovec *iov, int iovcnt) {
    if (iovcnt == 1 && iov && iov[0].iov_len >= 4 && iov[0].iov_len <= MAXIN) {
        const unsigned char *p = (const unsigned char *)iov[0].iov_base;
        if (p[0] == FRAME_START) {
            unsigned char out[2 * MAXIN];
            unsigned long o = process(p, iov[0].iov_len, out);
            raw_write(fd, out, o);
            return (long)iov[0].iov_len;
        }
    }
    return sys3(SYS_writev, fd, (long)iov, (long)iovcnt);
}
