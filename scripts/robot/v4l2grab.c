/*
 * v4l2grab.c — grab one frame from a V4L2 device and write it as a PPM (P6) image.
 *
 * For the Dreame D10s Pro front camera (OV8856) on /dev/video0. The sunxi-vin driver is a
 * MULTI-PLANE capture device (V4L2_CAP_VIDEO_CAPTURE_MPLANE), so the *_MPLANE API is used.
 * AVA streams the sensor on /dev/video2; /dev/video0 is a second VIN node (usually free).
 *
 * ⚠️ DEAD END — DO NOT RUN ON A LIVE ROBOT. Configuring /dev/video0 (S_FMT) while AVA holds the
 * sensor on /dev/video2 shares one ISP and DEADLOCKS the kernel (uninterruptible D-state, needs a
 * reboot) — observed and reproduced. The NV12 hardcode / lack of retry here are moot: the whole
 * video0-takeover route is superseded by the SAFE path (camsiphon read-only /dev/video2 tap →
 * camstream cedar-JPEG → go2rtc). Kept only as a record of the attempt. See docs/sensors.md.
 *
 * Build & run inside the Ubuntu chroot (links chroot glibc; /dev is bind-mounted there):
 *   gcc-13 -O2 -o /data/chroot/usr/local/bin/v4l2grab /tmp/v4l2grab.c
 *   chroot /data/chroot /usr/local/bin/v4l2grab /dev/video0 /tmp/frame.ppm
 *
 * Enumerates formats, reads the current format, captures a short burst (AE settle), keeps the
 * last frame, converts YUYV / NV12 / NV21 (1- or 2-plane) -> RGB PPM. Any other pixel format
 * (e.g. raw Bayer) is dumped to <out>.raw with the FourCC printed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define BURST 12
#define NBUF  4
#define MP    VIDEO_MAX_PLANES

static int xioctl(int fd, unsigned long req, void *arg) {
    int r; do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR); return r;
}
static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static void yuv2rgb(int y, int u, int v, unsigned char *p) {
    int d = u - 128, e = v - 128;
    p[0] = clamp8(y + ((91881 * e) >> 16));
    p[1] = clamp8(y - ((22554 * d + 46802 * e) >> 16));
    p[2] = clamp8(y + ((116130 * d) >> 16));
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/video0";
    const char *out = argc > 2 ? argv[2] : "/tmp/frame.ppm";
    const int TYPE = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
        printf("device: driver=%s caps=0x%08x\n", cap.driver, cap.capabilities);

    struct v4l2_fmtdesc fdesc; memset(&fdesc, 0, sizeof fdesc); fdesc.type = TYPE;
    printf("formats:");
    for (fdesc.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fdesc) == 0; fdesc.index++)
        printf(" %.4s", (char *)&fdesc.pixelformat);
    printf("\n");

    struct v4l2_format fmt; memset(&fmt, 0, sizeof fmt); fmt.type = TYPE;
    xioctl(fd, VIDIOC_G_FMT, &fmt);
    printf("current: %ux%u num_planes=%u\n", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.num_planes);

    /* video0 comes up unconfigured (0x0) — set a format. Width/height/fourcc overridable via argv. */
    unsigned reqW = argc > 3 ? (unsigned)atoi(argv[3]) : 640;
    unsigned reqH = argc > 4 ? (unsigned)atoi(argv[4]) : 480;
    memset(&fmt, 0, sizeof fmt); fmt.type = TYPE;
    fmt.fmt.pix_mp.width = reqW; fmt.fmt.pix_mp.height = reqH;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT"); return 2; }
    unsigned W = fmt.fmt.pix_mp.width, H = fmt.fmt.pix_mp.height, pf = fmt.fmt.pix_mp.pixelformat;
    unsigned nplanes = fmt.fmt.pix_mp.num_planes;
    printf("set: %ux%u %.4s num_planes=%u\n", W, H, (char *)&pf, nplanes);
    if (nplanes == 0) { fprintf(stderr, "S_FMT did not stick (pipeline likely not linked to video0)\n"); return 2; }

    struct v4l2_requestbuffers rb; memset(&rb, 0, sizeof rb);
    rb.count = NBUF; rb.type = TYPE; rb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &rb) < 0) { perror("REQBUFS"); return 2; }

    void *pmap[NBUF][MP]; unsigned plen[NBUF][MP];
    for (unsigned i = 0; i < rb.count; i++) {
        struct v4l2_buffer b; struct v4l2_plane planes[MP];
        memset(&b, 0, sizeof b); memset(planes, 0, sizeof planes);
        b.type = TYPE; b.memory = V4L2_MEMORY_MMAP; b.index = i; b.length = nplanes; b.m.planes = planes;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); return 2; }
        for (unsigned p = 0; p < nplanes; p++) {
            plen[i][p] = planes[p].length;
            pmap[i][p] = mmap(0, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, planes[p].m.mem_offset);
            if (pmap[i][p] == MAP_FAILED) { perror("mmap"); return 2; }
        }
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 2; }
    }

    int type = TYPE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); return 3; }

    unsigned char *P0 = 0, *P1 = 0; unsigned L0 = 0, L1 = 0;
    for (int n = 0; n < BURST; n++) {
        struct v4l2_buffer b; struct v4l2_plane planes[MP];
        memset(&b, 0, sizeof b); memset(planes, 0, sizeof planes);
        b.type = TYPE; b.memory = V4L2_MEMORY_MMAP; b.length = nplanes; b.m.planes = planes;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) { perror("DQBUF"); return 3; }
        if (n == BURST - 1) {
            L0 = plen[b.index][0]; P0 = malloc(L0); memcpy(P0, pmap[b.index][0], L0);
            if (nplanes > 1) { L1 = plen[b.index][1]; P1 = malloc(L1); memcpy(P1, pmap[b.index][1], L1); }
        }
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 3; }
    }
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    close(fd);

    if (pf == V4L2_PIX_FMT_YUYV || pf == V4L2_PIX_FMT_NV12 || pf == V4L2_PIX_FMT_NV21) {
        FILE *f = fopen(out, "wb"); if (!f) { perror("fopen"); return 4; }
        fprintf(f, "P6\n%u %u\n255\n", W, H);
        unsigned char px[3];
        if (pf == V4L2_PIX_FMT_YUYV) {
            for (unsigned i = 0; i + 3 < L0; i += 4) {
                yuv2rgb(P0[i], P0[i+1], P0[i+3], px); fwrite(px, 1, 3, f);
                yuv2rgb(P0[i+2], P0[i+1], P0[i+3], px); fwrite(px, 1, 3, f);
            }
        } else {
            unsigned char *Y = P0, *C = (nplanes > 1) ? P1 : (P0 + W * H);
            int uoff = (pf == V4L2_PIX_FMT_NV12) ? 0 : 1;
            for (unsigned y = 0; y < H; y++)
                for (unsigned x = 0; x < W; x++) {
                    unsigned ci = (y / 2) * W + (x & ~1u);
                    yuv2rgb(Y[y * W + x], C[ci + uoff], C[ci + (uoff ^ 1)], px);
                    fwrite(px, 1, 3, f);
                }
        }
        fclose(f);
        printf("wrote %s (%ux%u RGB PPM)\n", out, W, H);
    } else {
        char raw[256]; snprintf(raw, sizeof raw, "%s.raw", out);
        FILE *f = fopen(raw, "wb"); fwrite(P0, 1, L0, f); if (P1) fwrite(P1, 1, L1, f); fclose(f);
        printf("unhandled pixfmt %.4s — dumped raw to %s (%ux%u, %u planes)\n", (char *)&pf, raw, W, H, nplanes);
    }
    return 0;
}
