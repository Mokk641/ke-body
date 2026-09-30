#include "imgdec.h"
#include "camera.h"
#include "png.h"
#include "pics.h"
#include <stdlib.h>

const char *imgdec_ext(const uint8_t *d, size_t n)
{
    if (n > 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return "png";
    if (n > 3 && d[0] == 0xFF && d[1] == 0xD8) return "jpg";
    return NULL;
}

bool imgdec_decode(const uint8_t *d, size_t n, int max_w, int max_h, uint16_t **out, int *w, int *h)
{
    const char *ext = imgdec_ext(d, n);
    if (!ext) return false;
    if (ext[0] == 'p') return png_decode_rgb565(d, n, max_w, max_h, out, w, h);
    uint16_t *buf = NULL;
    int bw, bh;
    if (camera_decode_to_fit(d, n, max_w, max_h, &buf, &bw, &bh) != 0) return false;   /* ESP_OK == 0 */
    /* the JPEG decoder can only shrink by 2, 4, 8: fit exactly now */
    bool ok = rgb565_fit(buf, bw, bh, max_w, max_h, out, w, h);
    free(buf);
    return ok;
}
