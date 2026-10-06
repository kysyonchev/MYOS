#include "vga.h"
#include "screen.h"

static inline unsigned char inb(unsigned short p) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static inline void outb(unsigned short p, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}

/* VGA spec says allow 1-2 ISA cycles between register writes.
 * Reading port 0x3DA (input status 1) resets the attribute controller
 * flip-flop AND provides the required delay. */
static inline void io_delay(void) { (void)inb(0x3DA); }

static void wr_seq (unsigned char i, unsigned char v) { outb(0x3C4, i); io_delay(); outb(0x3C5, v); io_delay(); }
static void wr_gc  (unsigned char i, unsigned char v) { outb(0x3CE, i); io_delay(); outb(0x3CF, v); io_delay(); }
static void wr_crtc(unsigned char i, unsigned char v) { outb(0x3D4, i); io_delay(); outb(0x3D5, v); io_delay(); }
static void wr_ac  (unsigned char i, unsigned char v) { io_delay(); outb(0x3C0, i); outb(0x3C0, v); }

/* ---------- Mode 13h (320x200x256) ---------- */

static const unsigned char seq13[][2] = {
    {0x00, 0x03}, {0x01, 0x01}, {0x02, 0x0F}, {0x03, 0x00}, {0x04, 0x0E},
};

static const unsigned char crtc13[][2] = {
    {0x00, 0x5F}, {0x01, 0x4F}, {0x02, 0x50}, {0x03, 0x82},
    {0x04, 0x54}, {0x05, 0x80}, {0x06, 0xBF}, {0x07, 0x1F},
    {0x08, 0x00}, {0x09, 0x41}, {0x0A, 0x00}, {0x0B, 0x00},
    {0x0C, 0x00}, {0x0D, 0x00}, {0x0E, 0x00}, {0x0F, 0x00},
    {0x10, 0x9C}, {0x11, 0x8E}, {0x12, 0x8F}, {0x13, 0x28},
    {0x14, 0x40}, {0x15, 0x96}, {0x16, 0xB9}, {0x17, 0xA3},
    {0x18, 0xFF},
};

static const unsigned char gc13[][2] = {
    {0x00, 0x00}, {0x01, 0x00}, {0x02, 0x00}, {0x03, 0x00},
    {0x04, 0x00}, {0x05, 0x40}, {0x06, 0x05}, {0x07, 0x0F},
    {0x08, 0xFF},
};

static const unsigned char ac13[][2] = {
    {0x10, 0x41}, {0x11, 0x00}, {0x12, 0x0F}, {0x13, 0x00}, {0x14, 0x00},
};

/* ---------- Mode 3 (80x25 text) ---------- */

static const unsigned char seq03[][2] = {
    {0x00, 0x03}, {0x01, 0x00}, {0x02, 0x03}, {0x03, 0x00}, {0x04, 0x02},
};

static const unsigned char crtc03[][2] = {
    {0x00, 0x5F}, {0x01, 0x4F}, {0x02, 0x50}, {0x03, 0x82},
    {0x04, 0x55}, {0x05, 0x81}, {0x06, 0xBF}, {0x07, 0x1F},
    {0x08, 0x00}, {0x09, 0x4F}, {0x0A, 0x0D}, {0x0B, 0x0E},
    {0x0C, 0x00}, {0x0D, 0x00}, {0x0E, 0x00}, {0x0F, 0x00},
    {0x10, 0x9C}, {0x11, 0x8E}, {0x12, 0x8F}, {0x13, 0x28},
    {0x14, 0x1F}, {0x15, 0x96}, {0x16, 0xB9}, {0x17, 0xA3},
    {0x18, 0xFF},
};

static const unsigned char gc03[][2] = {
    {0x00, 0x00}, {0x01, 0x00}, {0x02, 0x00}, {0x03, 0x00},
    {0x04, 0x00}, {0x05, 0x10}, {0x06, 0x0E}, {0x07, 0x00},
    {0x08, 0xFF},
};

static const unsigned char ac03[][2] = {
    {0x10, 0x0C}, {0x11, 0x00}, {0x12, 0x0F}, {0x13, 0x08}, {0x14, 0x00},
};

static void enter_mode(unsigned char misc,
                      const unsigned char (*seq)[2],  int seqn,
                      const unsigned char (*crtc)[2], int crtcn,
                      const unsigned char (*gc)[2],   int gcn,
                      const unsigned char (*ac)[2],   int acn) {
    outb(0x3C4, 0x00); outb(0x3C5, 0x01);   /* async reset */
    io_delay();

    outb(0x3C2, misc);                       /* misc output */
    io_delay();

    /* Unlock CRTC 0..7 */
    outb(0x3D4, 0x11);
    io_delay();
    outb(0x3D5, (unsigned char)(inb(0x3D5) & 0x7F));
    io_delay();

    for (int i = 0; i < seqn;  i++) wr_seq (seq[i][0],  seq[i][1]);
    for (int i = 0; i < crtcn; i++) wr_crtc(crtc[i][0], crtc[i][1]);
    for (int i = 0; i < gcn;   i++) wr_gc  (gc[i][0],   gc[i][1]);
    for (int i = 0; i < acn;   i++) wr_ac  (ac[i][0],   ac[i][1]);
    wr_ac(0x20, 0x00);                       /* enable video output */

    outb(0x3C4, 0x00); outb(0x3C5, 0x03);   /* un-reset */
    io_delay();
    io_delay();
    io_delay();
}

/* Reset DAC to standard VGA text-mode palette (so text renders right after
 * coming back from mode 13h). */
static void set_text_palette(void) {
    static const unsigned char std[16][3] = {
        { 0, 0, 0}, { 0, 0,42}, { 0,42, 0}, { 0,42,42},
        {42, 0, 0}, {42, 0,42}, {42,21, 0}, {42,42,42},
        {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
        {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},
    };
    outb(0x3C8, 0);
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, std[i][0]);
        outb(0x3C9, std[i][1]);
        outb(0x3C9, std[i][2]);
    }
    /* Fill 16..255 with the default text-grey so nothing stale shows. */
    for (int i = 16; i < 256; i++) {
        outb(0x3C9, 0x15);
        outb(0x3C9, 0x15);
        outb(0x3C9, 0x15);
    }
}

void vga_set_mode13(void) {
    enter_mode(0x63,
               seq13,  sizeof(seq13) /2,
               crtc13, sizeof(crtc13)/2,
               gc13,   sizeof(gc13)  /2,
               ac13,   sizeof(ac13)  /2);
    vga_set_default_palette();
    vga_clear(0);
}

void vga_set_mode3(void) {
    enter_mode(0x67,
               seq03,  sizeof(seq03) /2,
               crtc03, sizeof(crtc03)/2,
               gc03,   sizeof(gc03)  /2,
               ac03,   sizeof(ac03)  /2);

    set_text_palette();

    /* Give the VGA a moment to settle, then wipe text memory. */
    for (volatile int i = 0; i < 200000; i++) { }

    unsigned short* txt = (unsigned short*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) txt[i] = 0x0720;

    /* Reset CRTC start address and cursor position to 0,0 */
    wr_crtc(0x0C, 0x00);
    wr_crtc(0x0D, 0x00);
    wr_crtc(0x0E, 0x00);
    wr_crtc(0x0F, 0x00);
}

/* ---------- Palette ---------- */

void vga_set_palette(unsigned char idx, unsigned char r, unsigned char g, unsigned char b) {
    outb(0x3C8, idx);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
}

void vga_set_default_palette(void) {
    static const unsigned char std[16][3] = {
        { 0, 0, 0}, { 0, 0,42}, { 0,42, 0}, { 0,42,42},
        {42, 0, 0}, {42, 0,42}, {42,21, 0}, {42,42,42},
        {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
        {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},
    };
    for (int i = 0; i < 16; i++)
        vga_set_palette((unsigned char)i, std[i][0], std[i][1], std[i][2]);
    for (int i = 0; i < 16; i++) {
        unsigned char v = (unsigned char)(i * 4);
        vga_set_palette((unsigned char)(16 + i), v, v, v);
    }
    for (int i = 32; i < 256; i++) {
        unsigned char r = (unsigned char)((i * 3) & 0x3F);
        unsigned char g = (unsigned char)(((i * 5) >> 1) & 0x3F);
        unsigned char b = (unsigned char)(((i * 7) >> 2) & 0x3F);
        vga_set_palette((unsigned char)i, r, g, b);
    }
}

/* ---------- Framebuffer ---------- */

void vga_clear(unsigned char color) {
    unsigned char* fb = (unsigned char*)VGA13_FB;
    unsigned int n = VGA13_WIDTH * VGA13_HEIGHT;
    for (unsigned int i = 0; i < n; i++) fb[i] = color;
}

void vga_pixel(int x, int y, unsigned char color) {
    if (x < 0 || x >= VGA13_WIDTH || y < 0 || y >= VGA13_HEIGHT) return;
    ((unsigned char*)VGA13_FB)[y * VGA13_WIDTH + x] = color;
}

/* ---------- Vsync + blit ---------- */

void vga_vsync(void) {
    /* Wait for the start of vertical retrace, then the end.  That puts us
     * at the beginning of the blanking period where the framebuffer is not
     * being scanned out. */
    while ( inb(0x3DA) & 0x08) { }   /* wait for end of any current vblank */
    while (!(inb(0x3DA) & 0x08)) { } /* wait for start of next vblank */
}

void vga_blit(const unsigned char* src) {
    /* Copy 320x200 bytes into the linear framebuffer as fast as we can.
     * Using 32-bit words, four bytes at a time. */
    unsigned int* dst32 = (unsigned int*)VGA13_FB;
    const unsigned int* src32 = (const unsigned int*)src;
    unsigned int n = (VGA13_WIDTH * VGA13_HEIGHT) / 4;   /* 16000 dwords */
    for (unsigned int i = 0; i < n; i++) dst32[i] = src32[i];
}
