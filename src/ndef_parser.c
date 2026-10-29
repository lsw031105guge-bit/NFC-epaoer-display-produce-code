/*
 * Minimal NDEF parser: record iterator plus a helper that extracts the text
 * of the first Text record from an NDEF message.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "ndef_parser.h"

/* NDEF record header bits */
#define NDEF_MB_MASK	0x80 /* Message Begin */
#define NDEF_ME_MASK	0x40 /* Message End */
#define NDEF_SR_MASK	0x10 /* Short Record */
#define NDEF_IL_MASK	0x08 /* ID Length present */

/* Text record: type "T", payload = [status][language code][text] */
#define NDEF_TEXT_TYPE      'T'
#define NDEF_TEXT_LANG_LEN_MASK 0x3F

static uint32_t get_be32(const uint8_t buf[4])
{
	return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
	       ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

/* Load the record at rec->pos into rec; advance rec->pos past it.
 * Returns 0, -ENOENT (end of message) or -EINVAL (malformed). */
static int ndef_record_load(struct ndef_record *rec)
{
	const uint8_t *msg = rec->msg;
	const size_t len = rec->msg_len;
	size_t pos = rec->pos;

	if (pos >= len) {
		return -ENOENT;
	}

	const uint8_t header = msg[pos];
	const bool short_record = (header & NDEF_SR_MASK) != 0;
	const bool id_present = (header & NDEF_IL_MASK) != 0;
	const bool message_end = (header & NDEF_ME_MASK) != 0;

	rec->tnf = header & 0x07;
	pos++;

	if (pos >= len) {
		return -EINVAL;
	}
	const uint8_t type_len = msg[pos++];

	uint32_t payload_len;
	if (short_record) {
		if (pos >= len) {
			return -EINVAL;
		}
		payload_len = msg[pos++];
	} else {
		if (pos + sizeof(uint32_t) > len) {
			return -EINVAL;
		}
		payload_len = get_be32(&msg[pos]);
		pos += sizeof(uint32_t);
	}

	uint8_t id_len = 0;
	if (id_present) {
		if (pos >= len) {
			return -EINVAL;
		}
		id_len = msg[pos++];
	}

	if ((size_t)(pos + type_len + id_len) > len ||
	    payload_len > len - pos - type_len - id_len) {
		return -EINVAL;
	}

	rec->type = &msg[pos];
	rec->type_len = type_len;
	pos += type_len + id_len;

	rec->payload = &msg[pos];
	rec->payload_len = payload_len;
	pos += payload_len;

	/* Stop at the last record even if the buffer has trailing bytes. */
	rec->pos = message_end ? len : pos;

	return 0;
}

int ndef_record_first(const uint8_t *msg, size_t len, struct ndef_record *rec)
{
	if (msg == NULL || rec == NULL || len == 0) {
		return -EINVAL;
	}

	memset(rec, 0, sizeof(*rec));
	rec->msg = msg;
	rec->msg_len = len;
	rec->pos = 0;

	return ndef_record_load(rec);
}

int ndef_record_next(struct ndef_record *rec)
{
	if (rec == NULL || rec->msg == NULL) {
		return -EINVAL;
	}

	return ndef_record_load(rec);
}

int ndef_text_parse(const uint8_t *ndef_msg, size_t len,
		    char *text, size_t text_size)
{
	if (ndef_msg == NULL || text == NULL || text_size == 0) {
		return -EINVAL;
	}

	struct ndef_record rec;
	int err = ndef_record_first(ndef_msg, len, &rec);

	while (err == 0) {
		if (rec.tnf == NDEF_TNF_WELL_KNOWN &&
		    rec.type_len == 1 && rec.type[0] == NDEF_TEXT_TYPE) {
			if (rec.payload_len == 0) {
				return -EINVAL;
			}

			uint8_t status = rec.payload[0];
			uint8_t lang_len = status & NDEF_TEXT_LANG_LEN_MASK;

			if (rec.payload_len < (1 + lang_len)) {
				return -EINVAL;
			}

			size_t text_len = rec.payload_len - 1 - lang_len;
			if (text_len > text_size - 1) {
				text_len = text_size - 1; /* truncate for display */
			}

			memcpy(text, &rec.payload[1 + lang_len], text_len);
			text[text_len] = '\0';
			return 0;
		}
		err = ndef_record_next(&rec);
	}

	return err == -ENOENT ? -ENOENT : err;
}
