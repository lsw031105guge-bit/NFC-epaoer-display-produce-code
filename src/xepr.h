/*
 * XEPR image protocol: extraction of a framebuffer from an NDEF message,
 * in either of the two web tool encodings:
 *
 *  - Binary MIME record "application/x-xiao-epaper", payload = XEPR frame
 *    (header + image + CRC32, see below).
 *  - Text record with content "XEPR1|256|122|<hex of the framebuffer>"
 *    (BW format only).
 *  - 4C split text: the full 4C hex encoding (16k chars) is too large for
 *    one NDEF record, so it arrives as two Text records, "XEPR2A|<crc32
 *    hex8>|<first 4000 image bytes as hex>" and "XEPR2B|<last 4000 image
 *    bytes as hex>", written one after the other (either order). The two
 *    halves are buffered and assembled; the CRC32 of the reconstructed
 *    frame is verified before the image is accepted.
 *
 * Two image formats share the same 12-byte frame header
 * "XEPR"|ver(1)|w(2BE)|h(2BE)|fmt(1)|len(2BE) followed by the image and a
 * CRC32 (4BE) over everything before it:
 *
 *  fmt 1 (BW):  256x122 1bpp row-major, MSB first, 1 = white (3904 bytes).
 *  fmt 2 (4C):  128x250 2bpp mirroring the JD79661 panel RAM (8000 bytes):
 *               250 gate lines x 32 bytes (128 sources, 2 bits each);
 *               within a byte source 0 is bits [7:6], source 1 [5:4], ...
 *               0x00 black, 0x01 white, 0x02 yellow, 0x03 red.
 */

#ifndef XEPR_H__
#define XEPR_H__

#include <stddef.h>
#include <stdint.h>

/** Image format codes (frame header field "fmt"). */
#define XEPR_FORMAT_BW1 1
#define XEPR_FORMAT_4C2 2

/* --- BW format (256x122 1bpp row-major, MSB first, 1 = white) --- */

/** Total length of a binary XEPR frame (header + image + CRC32). */
#define XEPR_FRAME_LEN 3920

/** Image payload length: 256 x 122 / 8, one row = 32 bytes. */
#define XEPR_IMAGE_LEN 3904

/** Framebuffer geometry (row-aligned width, actual panel is 250x122). */
#define XEPR_FB_W        256
#define XEPR_FB_H        122
#define XEPR_FB_ROW_BYTES 32

/* --- 4C format (128x250 2bpp, mirrors the JD79661 panel RAM) --- */

/** Total length of a 4C XEPR frame (header + image + CRC32). */
#define XEPR4C_FRAME_LEN 8016

/** Image payload length: 250 gate lines x 32 bytes (128 sources, 2bpp). */
#define XEPR4C_IMAGE_LEN 8000

/** Frame geometry as stored in the frame header: sources x gates. */
#define XEPR4C_FB_W        128
#define XEPR4C_FB_H        250
#define XEPR4C_FB_ROW_BYTES 32

/** Largest image payload across formats (size of the caller's buffer). */
#define XEPR_FB_MAX_LEN XEPR4C_IMAGE_LEN

/**
 * @brief xepr_from_ndef() result: one half of a split 4C text image was
 *        accepted and buffered, the other half is still pending.
 *
 * Not an errno: deliberately outside the errno range.
 */
#define XEPR_ERR_WAIT (-2000)

/**
 * @brief CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) over a buffer.
 *
 * Exposed for other modules (the persistent content store CRCs the
 * saved image).
 *
 * @param data Buffer.
 * @param len  Length in bytes.
 *
 * @return CRC-32 of the buffer.
 */
uint32_t xepr_crc32(const uint8_t *data, size_t len);

/**
 * @brief Validate a binary XEPR frame (magic, fields, CRC32).
 *
 * @param[in] frame   Frame buffer.
 * @param[in] len     Available length.
 * @param[out] fb     Set to the image payload inside the frame.
 * @param[out] format Set to XEPR_FORMAT_BW1 or XEPR_FORMAT_4C2.
 *
 * @retval 0        Frame valid, @p fb and @p format set.
 * @retval -EINVAL  Wrong size, magic, version, geometry or CRC.
 */
int xepr_frame_parse(const uint8_t *frame, size_t len, const uint8_t **fb,
		     uint8_t *format);

/**
 * @brief Parse the hex text encoding "XEPR1|<w>|<h>|<HEX>" (BW only).
 *
 * @param[in] text     Text record content (after status/language bytes).
 * @param[in] len      Length of the text.
 * @param[out] fb      Output framebuffer, XEPR_IMAGE_LEN bytes.
 * @param[in] fb_size  Size of the output buffer.
 *
 * @retval 0        Decoded, @p fb filled.
 * @retval -EINVAL  Bad prefix, dimensions or hex data.
 */
int xepr_hex_text_parse(const char *text, size_t len,
			uint8_t *fb, size_t fb_size);

/**
 * @brief Search an NDEF message for an XEPR image and decode it.
 *
 * The first matching record wins: a MIME record of type
 * "application/x-xiao-epaper" (binary frame, any format), or a Text record
 * whose content starts with "XEPR1|" (BW hex encoding) or "XEPR2A|"/
 * "XEPR2B|" (split 4C encoding, buffered between calls).
 *
 * @param[in] msg      NDEF message (record stream, no NLEN file prefix).
 * @param[in] len      Length of the NDEF message.
 * @param[out] fb      Output framebuffer, XEPR_FB_MAX_LEN bytes.
 * @param[in] fb_size  Size of the output buffer.
 * @param[out] format  Set to XEPR_FORMAT_BW1 or XEPR_FORMAT_4C2.
 *
 * @retval 0        Image decoded, @p fb filled and @p format set.
 * @retval -ENOENT  No XEPR record in the message.
 * @retval -EINVAL  XEPR record present but invalid (bad frame/CRC/hex).
 * @retval XEPR_ERR_WAIT  Split 4C half buffered, other half pending.
 */
int xepr_from_ndef(const uint8_t *msg, size_t len, uint8_t *fb, size_t fb_size,
		   uint8_t *format);

#endif /* XEPR_H__ */
