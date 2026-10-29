/*
 * Minimal NDEF parser: record iterator plus a helper that extracts the text
 * of the first Text record from an NDEF message.
 */

#ifndef NDEF_PARSER_H__
#define NDEF_PARSER_H__

#include <stddef.h>
#include <stdint.h>

/** Type Name Format (TNF) values. */
#define NDEF_TNF_EMPTY        0x00
#define NDEF_TNF_WELL_KNOWN   0x01
#define NDEF_TNF_MEDIA        0x02
#define NDEF_TNF_ABSOLUTE_URI 0x03
#define NDEF_TNF_EXTERNAL     0x04

/** One parsed NDEF record; also holds the iterator state. */
struct ndef_record {
	uint8_t tnf;            /**< Type Name Format */
	const uint8_t *type;    /**< Record type (points into the message) */
	size_t type_len;
	const uint8_t *payload; /**< Record payload (points into the message) */
	size_t payload_len;

	/* Iterator state (internal). */
	const uint8_t *msg;     /**< Start of the NDEF message */
	size_t msg_len;         /**< Length of the NDEF message */
	size_t pos;             /**< Offset of the next record header */
};

/**
 * @brief Start iterating over the records of an NDEF message.
 *
 * @param[in] msg  NDEF message (record stream, no NLEN file prefix).
 * @param[in] len  Length of the NDEF message.
 * @param[out] rec Iterator; holds the first record on success.
 *
 * @retval 0        First record loaded into @p rec.
 * @retval -EINVAL  Malformed NDEF message.
 * @retval -ENOENT  Empty message.
 */
int ndef_record_first(const uint8_t *msg, size_t len, struct ndef_record *rec);

/**
 * @brief Advance the iterator to the next record.
 *
 * @param[inout] rec  Iterator from ndef_record_first().
 *
 * @retval 0        Next record loaded into @p rec.
 * @retval -ENOENT  End of message (or last record had ME set).
 * @retval -EINVAL  Malformed record.
 */
int ndef_record_next(struct ndef_record *rec);

/**
 * @brief Parse an NDEF message and extract the text of the first Text record
 *        (TNF = Well Known, type = "T").
 *
 * @param[in]  ndef_msg   NDEF message (record stream, no NLEN file prefix).
 * @param[in]  len        Length of the NDEF message.
 * @param[out] text       Output buffer for the extracted text (NUL-terminated).
 *                        Truncated if the text is longer than the buffer.
 * @param[in]  text_size  Size of the output buffer.
 *
 * @retval 0         Text extracted.
 * @retval -EINVAL   Malformed NDEF message.
 * @retval -ENOENT   No Text record in the message.
 */
int ndef_text_parse(const uint8_t *ndef_msg, size_t len,
		    char *text, size_t text_size);

#endif /* NDEF_PARSER_H__ */
