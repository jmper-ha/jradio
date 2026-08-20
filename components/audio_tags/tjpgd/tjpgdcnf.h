/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/

/* This is jradio's own configuration, and it is the reason the decoder is
 * vendored here rather than taken from a component: the two copies already in
 * the tree are configured for somebody else. LVGL's ships with descaling off
 * and RGB888 output, and espressif/esp_new_jpeg - which does both - puts its
 * assembly kernels in .iram1, where they cost 6.3 KB of internal RAM whether a
 * cover is ever decoded or not. That was enough to push the largest free
 * DMA-capable block from 17408 to 8704 bytes, below the 8 KB that mbedtls
 * needs for AES, and every HTTPS station stopped connecting.
 */

#define JD_SZBUF        512
/* Specifies size of stream input buffer */

#define JD_FORMAT       1
/* Output is RGB565 - the display's own format and LVGL's, so the pixels the
/  decoder writes are the pixels that get drawn, with no second conversion.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)
*/

#define JD_USE_SCALE    1
/* Descaling by 1/2, 1/4 or 1/8 while decoding. A 250-pixel cover is decoded
/  straight to 125 for a 96-pixel tile, so the intermediate is 31 KB instead of
/  125 KB and only one averaging pass is left to do.
/  0: Disable
/  1: Enable
*/

#define JD_TBLCLIP      1
/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
*/

#define JD_FASTDECODE   1
/* 1 rather than 2: the huffman lookup tables 2 builds want 6 KB of the work
/  pool, and a cover is decoded once per track, not once per frame.
/  0: Basic optimization. Suitable for 8/16-bit MCUs.
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM)
*/
