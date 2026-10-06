#ifndef VGA_H
#define VGA_H

#define VGA13_WIDTH   320
#define VGA13_HEIGHT  200
#define VGA13_FB      0xA0000

void vga_set_mode13(void);
void vga_set_mode3(void);
void vga_set_palette(unsigned char idx, unsigned char r, unsigned char g, unsigned char b);
void vga_set_default_palette(void);
void vga_clear(unsigned char color);
void vga_pixel(int x, int y, unsigned char color);
void vga_vsync(void);
void vga_blit(const unsigned char* buffer);

#endif
