/*
 * XEPR image protocol: extraction of a 256x122 1-bit framebuffer from an
 * NDEF message. See xepr.h for the supported encodings.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "ndef_parser.h"
#include "xepr.h"

#define XEPR_MAGIC       "XEPR"
#define XEPR_MAGIC_LEN   4
#define XEPR_VERSION     1

#define XEPR_MIME_TYPE   "application/x-xiao-epaper"

/* Text encoding prefix: "XEPR1|" */
#define XEPR_TEXT_PREFIX    "XEPR1|"
#define XEPR_TEXT_PREFIX_LEN 6

/* Frame layout: "XEPR"|ver|w(2BE)|h(2BE)|fmt|len(2BE)|image|crc32(4BE) */
#define XEPR_HEADER_LEN  12

static uint32_t get_be32(const uint8_t buf[4])
{
	return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
	       ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

/* CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) — same as the web tool.
 * xepr_crc32() runs the final XOR; the split-4C assembly needs to continue
 * an intermediate state across two buffers, hence the split into a core.
 */
static uint32_t xepr_crc32_core(uint32_t c, const uint8_t *data, size_t len)
{
	static uint32_t table[256];
	static bool table_ready;

	if (!table_ready) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t v = n;
			for (int k = 0; k < 8; k++) {
				v = (v & 1) ? (0xEDB88320 ^ (v >> 1)) : (v >> 1);
			}
			table[n] = v;
		}
		table_ready = true;
	}

	for (size_t i = 0; i < len; i++) {
		c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
	}
	return c;
}

uint32_t xepr_crc32(const uint8_t *data, size_t len)
{
	return xepr_crc32_core(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF;
}

int xepr_frame_parse(const uint8_t *frame, size_t len, const uint8_t **fb,
		     uint8_t *format)
{
	if (frame == NULL || fb == NULL || format == NULL ||
	    len < XEPR_HEADER_LEN) {
		return -EINVAL;
	}

	if (memcmp(frame, XEPR_MAGIC, XEPR_MAGIC_LEN) != 0) {
		return -EINVAL;
	}
	if (frame[4] != XEPR_VERSION) {
		return -EINVAL;
	}

	uint16_t width = ((uint16_t)frame[5] << 8) | frame[6];
	uint16_t height = ((uint16_t)frame[7] << 8) | frame[8];
	uint8_t format_code = frame[9];
	uint16_t image_len = ((uint16_t)frame[10] << 8) | frame[11];

	/* Geometry and image size must match the format exactly. */
	if (format_code == XEPR_FORMAT_BW1) {
		if (width != XEPR_FB_W || height != XEPR_FB_H ||
		    image_len != XEPR_IMAGE_LEN) {
			return -EINVAL;
		}
	} else if (format_code == XEPR_FORMAT_4C2) {
		if (width != XEPR4C_FB_W || height != XEPR4C_FB_H ||
		    image_len != XEPR4C_IMAGE_LEN) {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	size_t frame_len = XEPR_HEADER_LEN + image_len + 4;
	if (len < frame_len) {
		return -EINVAL;
	}

	size_t crc_offset = frame_len - 4;
	uint32_t crc_calc = xepr_crc32(frame, crc_offset);
	uint32_t crc_exp = get_be32(&frame[crc_offset]);
	if (crc_calc != crc_exp) {
		return -EINVAL;
	}

	*fb = frame + XEPR_HEADER_LEN;
	*format = format_code;
	return 0;
}

static int hex_val(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

/* Decode exactly n bytes of hex into dst; tolerate trailing CR/LF written
 * by some NFC writer apps.
 */
static int hex_decode(const char *text, size_t len, uint8_t *dst, size_t n)
{
	while (len > 0 &&
	       (text[len - 1] == '\r' || text[len - 1] == '\n')) {
		len--;
	}
	if (len < n * 2) {
		return -EINVAL;
	}

	for (size_t i = 0; i < n; i++) {
		int hi = hex_val(text[2 * i]);
		int lo = hex_val(text[2 * i + 1]);
		if (hi < 0 || lo < 0) {
			return -EINVAL;
		}
		dst[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

int xepr_hex_text_parse(const char *text, size_t len,
			uint8_t *fb, size_t fb_size)
{
	if (text == NULL || fb == NULL || fb_size < XEPR_IMAGE_LEN) {
		return -EINVAL;
	}
	if (len < XEPR_TEXT_PREFIX_LEN || strncmp(text, XEPR_TEXT_PREFIX,
						  XEPR_TEXT_PREFIX_LEN) != 0) {
		return -EINVAL;
	}

	/* Parse "<w>|<h>|" after the prefix. */
	size_t pos = XEPR_TEXT_PREFIX_LEN;
	uint32_t dims[2];

	for (int i = 0; i < 2; i++) {
		uint32_t v = 0;
		size_t digits = 0;

		while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
			v = v * 10 + (text[pos] - '0');
			pos++;
			digits++;
		}
		if (digits == 0 || digits > 5 || pos >= len || text[pos] != '|') {
			return -EINVAL;
		}
		pos++; /* skip '|' */
		dims[i] = v;
	}
	if (dims[0] != XEPR_FB_W || dims[1] != XEPR_FB_H) {
		return -EINVAL;
	}

	return hex_decode(&text[pos], len - pos, fb, XEPR_IMAGE_LEN);
}

/* --- 4C split text encoding ----------------------------------------------
 *
 * The full 4C hex encoding (16k chars) does not fit in one NDEF Text
 * record, so the web tool splits it into two separate writes:
 *
 *   A: "XEPR2A|<CRC32 of the frame as 8 hex chars>|<image[0..4000) hex>"
 *   B: "XEPR2B|<image[4000..8000) hex>"
 *
 * Each write is ~8k chars. The halves are buffered in chunk_img; once
 * both are present the CRC32 over the reconstructed frame (fixed 4C
 * header + image) is verified, so halves from different images can never
 * assemble silently. Either write order works.
 */
#define XEPR4C_CHUNK_PREFIX_A   "XEPR2A|"
#define XEPR4C_CHUNK_PREFIX_B   "XEPR2B|"
#define XEPR4C_CHUNK_PREFIX_LEN 7
#define XEPR4C_CRC_HEX_LEN      8
#define XEPR4C_HALF_LEN         (XEPR4C_IMAGE_LEN / 2)

static uint8_t chunk_img[XEPR4C_IMAGE_LEN];
static uint32_t chunk_crc;
static bool chunk_have_a, chunk_have_b;

/* Fixed header of the 4C frame ("XEPR"|1|128|250|2|8000) for the CRC. */
static const uint8_t chunk_header[XEPR_HEADER_LEN] = {
	'X', 'E', 'P', 'R', XEPR_VERSION,
	(XEPR4C_FB_W >> 8) & 0xFF, XEPR4C_FB_W & 0xFF,
	(XEPR4C_FB_H >> 8) & 0xFF, XEPR4C_FB_H & 0xFF,
	XEPR_FORMAT_4C2,
	(XEPR4C_IMAGE_LEN >> 8) & 0xFF, XEPR4C_IMAGE_LEN & 0xFF,
};

/* Both halves present: verify the frame CRC and hand the image over.
 * State is always cleared (a mismatch requires restarting with part A).
 */
static int chunk_complete(uint8_t *fb, size_t fb_size, uint8_t *format)
{
	uint32_t c = xepr_crc32_core(0xFFFFFFFF, chunk_header,
				      XEPR_HEADER_LEN);
	c = xepr_crc32_core(c, chunk_img, XEPR4C_IMAGE_LEN) ^ 0xFFFFFFFF;

	chunk_have_a = false;
	chunk_have_b = false;

	if (c != chunk_crc) {
		return -EINVAL;
	}
	if (fb_size < XEPR4C_IMAGE_LEN) {
		return -EINVAL;
	}
	memcpy(fb, chunk_img, XEPR4C_IMAGE_LEN);
	*format = XEPR_FORMAT_4C2;
	return 0;
}

static int xepr_chunk_text_parse(const char *text, size_t len,
				 uint8_t *fb, size_t fb_size, uint8_t *format)
{
	if (len <= XEPR4C_CHUNK_PREFIX_LEN) {
		return -EINVAL;
	}

	if (memcmp(text, XEPR4C_CHUNK_PREFIX_A,
		   XEPR4C_CHUNK_PREFIX_LEN) == 0) {
		size_t pos = XEPR4C_CHUNK_PREFIX_LEN;
		uint32_t crc = 0;

		for (int i = 0; i < XEPR4C_CRC_HEX_LEN; i++) {
			int v = hex_val(text[pos + i]);
			if (v < 0) {
				return -EINVAL;
			}
			crc = (crc << 4) | (uint32_t)v;
		}
		pos += XEPR4C_CRC_HEX_LEN;
		if (pos >= len || text[pos] != '|') {
			return -EINVAL;
		}
		pos++;

		/* A carries the frame CRC and the first half. A stale B from
		 * a previous image is kept: the CRC check at completion
		 * catches (and rejects) the mix.
		 */
		if (hex_decode(&text[pos], len - pos, chunk_img,
			       XEPR4C_HALF_LEN) != 0) {
			return -EINVAL;
		}
		chunk_crc = crc;
		chunk_have_a = true;
		return chunk_have_b ? chunk_complete(fb, fb_size, format)
				    : XEPR_ERR_WAIT;
	}

	if (memcmp(text, XEPR4C_CHUNK_PREFIX_B,
		   XEPR4C_CHUNK_PREFIX_LEN) == 0) {
		if (hex_decode(&text[XEPR4C_CHUNK_PREFIX_LEN],
			       len - XEPR4C_CHUNK_PREFIX_LEN,
			       chunk_img + XEPR4C_HALF_LEN,
			       XEPR4C_HALF_LEN) != 0) {
			return -EINVAL;
		}
		chunk_have_b = true;
		return chunk_have_a ? chunk_complete(fb, fb_size, format)
				    : XEPR_ERR_WAIT;
	}

	return -EINVAL;
}

int xepr_from_ndef(const uint8_t *msg, size_t len, uint8_t *fb, size_t fb_size,
		   uint8_t *format)
{
	if (msg == NULL || fb == NULL || format == NULL ||
	    fb_size < XEPR_IMAGE_LEN) {
		return -EINVAL;
	}

	struct ndef_record rec;
	int err = ndef_record_first(msg, len, &rec);

	while (err == 0) {
		/* Binary path: MIME record with a full XEPR frame. */
		if (rec.tnf == NDEF_TNF_MEDIA &&
		    rec.type_len == sizeof(XEPR_MIME_TYPE) - 1 &&
		    memcmp(rec.type, XEPR_MIME_TYPE, rec.type_len) == 0) {
			const uint8_t *src;
			uint8_t fmt;

			if (xepr_frame_parse(rec.payload, rec.payload_len,
					     &src, &fmt) != 0) {
				return -EINVAL;
			}
			if ((fmt == XEPR_FORMAT_4C2 &&
			     fb_size < XEPR4C_IMAGE_LEN)) {
				return -EINVAL;
			}
			memcpy(fb, src, fmt == XEPR_FORMAT_4C2
					      ? XEPR4C_IMAGE_LEN
					      : XEPR_IMAGE_LEN);
			*format = fmt;
			return 0;
		}

		/* Text path: XEPR hex encodings. */
		if (rec.tnf == NDEF_TNF_WELL_KNOWN &&
		    rec.type_len == 1 && rec.type[0] == 'T' &&
		    rec.payload_len > 1 + XEPR_TEXT_PREFIX_LEN) {
			uint8_t lang_len = rec.payload[0] & 0x3F;
			const uint8_t *text = rec.payload + 1 + lang_len;
			size_t text_len = rec.payload_len - 1 - lang_len;

			/* Split 4C encoding: "XEPR2A|..."/"XEPR2B|...". */
			if (text_len > XEPR4C_CHUNK_PREFIX_LEN &&
			    (memcmp(text, XEPR4C_CHUNK_PREFIX_A,
				    XEPR4C_CHUNK_PREFIX_LEN) == 0 ||
			     memcmp(text, XEPR4C_CHUNK_PREFIX_B,
				    XEPR4C_CHUNK_PREFIX_LEN) == 0)) {
				return xepr_chunk_text_parse(
					(const char *)text, text_len, fb,
					fb_size, format);
			}

			if (text_len > XEPR_TEXT_PREFIX_LEN &&
			    memcmp(text, XEPR_TEXT_PREFIX,
				   XEPR_TEXT_PREFIX_LEN) == 0) {
				int text_err = xepr_hex_text_parse(
					(const char *)text, text_len, fb,
					fb_size);

				if (text_err == 0) {
					*format = XEPR_FORMAT_BW1;
				}
				return text_err;
			}
		}

		err = ndef_record_next(&rec);
	}

	return err == -ENOENT ? -ENOENT : err;
}
