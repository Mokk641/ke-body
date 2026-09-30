/* Image rotation for the camera (pure C, host-testable). */
#pragma once
#include <stdint.h>

/* Rotate a w x h RGB565 image clockwise by rot (0/90/180/270) into dst (h x w for 90/270), swapping the two
 * bytes of every pixel on the way (decoder output is little-endian, the LCD / fmt2jpg want high byte first). */
void img_rotate_swap(const uint16_t *src, int w, int h, uint16_t *dst, int rot);
