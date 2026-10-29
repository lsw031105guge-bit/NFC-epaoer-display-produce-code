/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *
 * @ingroup nfc_writable_ndef_msg_example_ndef_file_m ndef_file_m.c
 * @{
 * @ingroup nfc_writable_ndef_msg_example
 * @brief FLASH service for the NFC writable NDEF message example.
 *
 */

#include <zephyr/kernel.h>
#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/fs/nvs.h>
#include <errno.h>
#include <string.h>
#include <nfc/t4t/ndef_file.h>
#include <nfc/ndef/uri_msg.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#if defined(CONFIG_NVS)
#include <zephyr/fs/nvs.h>
#elif defined(CONFIG_ZMS)
#include <zephyr/fs/zms.h>
#endif /* defined(CONFIG_NVS) */
#include "ndef_file_m.h"
#include "xepr.h"

#define FLASH_URL_ADDRESS_ID 1 /**< Address of URL message in FLASH */

/* ---- Persistent content store (ZMS) ------------------------------------
 *
 * Entries: layout marker, content metadata, image chunks. The image is
 * split into chunks because a single ZMS entry is limited to about one
 * sector (4096 bytes on this RRAM) minus ATE overhead.
 */
#define CS_META_ID      2 /**< Content metadata (commit point) */
#define CS_IMG0_ID      3 /**< Image bytes [0, CS_CHUNK_LEN) */
#define CS_IMG1_ID      4 /**< Image bytes [CS_CHUNK_LEN, len) */
#define CS_LAYOUT_ID    5 /**< Storage layout marker */
#define CS_LAYOUT_MAGIC 0x53504331

/* Max ZMS entry size is sector_size - 5 * ate_size (a little over 4000);
 * stay safely below it.
 */
#define CS_CHUNK_LEN    4000
#define CS_TEXT_MAX     256
#define CS_MAGIC        0x53504558

/** Content metadata: written last, its magic + kind select the content. */
struct cs_header {
	uint32_t magic;  /**< CS_MAGIC */
	uint8_t kind;    /**< CONTENT_STORE_KIND_* */
	uint8_t fmt;     /**< XEPR_FORMAT_* (images only) */
	uint16_t len;    /**< Image bytes / text bytes incl. NUL */
	uint32_t crc;    /**< CRC32 of the image (xepr_crc32) */
};

static const uint8_t m_url[] = /**< Default NDEF message: URL "nordicsemi.com". */
	{'n', 'o', 'r', 'd', 'i', 'c', 's', 'e', 'm', 'i', '.', 'c', 'o', 'm'};

/* Flash partition for storage*/
#define FLASH_DEVICE FIXED_PARTITION_DEVICE(storage_partition)
/* Flash block size in bytes */
#define SECTOR_SIZE  (DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size))
/* 6 of the 36 KiB partition: NDEF file + image chunks + metadata with
 * room for garbage collection.
 */
#define SECTOR_COUNT 6
/* Start address of the filesystem in flash */
#define STORAGE_OFFSET FIXED_PARTITION_OFFSET(storage_partition)

#if defined(CONFIG_NVS)
static struct nvs_fs fs = {
	.sector_size = SECTOR_SIZE,
	.sector_count = SECTOR_COUNT,
	.offset = STORAGE_OFFSET,
};
#elif defined(CONFIG_ZMS)
static struct zms_fs fs = {
	.sector_size = SECTOR_SIZE,
	.sector_count = SECTOR_COUNT,
	.offset = STORAGE_OFFSET,
};
#else
#error "Please select file system."
#endif /* defined(CONFIG_NVS) */

#if defined(CONFIG_ZMS)
/** Erase the whole storage partition (sector by sector, like ZMS GC). */
static int storage_wipe(void)
{
	for (uint32_t s = 0; s < SECTOR_COUNT; s++) {
		int err = flash_erase(FLASH_DEVICE,
				      STORAGE_OFFSET + s * SECTOR_SIZE,
				      SECTOR_SIZE);

		if (err) {
			return err;
		}
	}
	return 0;
}
#endif /* defined(CONFIG_ZMS) */

int ndef_file_setup(void)
{
	int err;

	fs.flash_device = FLASH_DEVICE;
	if (fs.flash_device == NULL) {
		return -ENODEV;
	}

#if defined(CONFIG_NVS)
	err = nvs_mount(&fs);
#elif defined(CONFIG_ZMS)
	err = zms_mount(&fs);

	if (err == 0) {
		uint32_t magic;

		if (zms_read(&fs, CS_LAYOUT_ID, &magic, sizeof(magic)) ==
			    (ssize_t)sizeof(magic) &&
		    magic == CS_LAYOUT_MAGIC) {
			return 0; /* known layout: keep the stored content */
		}
	}

	/* Unknown layout (first boot of this layout, or data written by an
	 * older firmware with a different sector count): erase the
	 * partition once and start over. The stored NDEF/content is lost;
	 * ndef_file_load() recreates the default NDEF message.
	 */
	err = storage_wipe();
	if (err == 0) {
		err = zms_mount(&fs);
	}
	if (err == 0) {
		uint32_t magic = CS_LAYOUT_MAGIC;

		err = zms_write(&fs, CS_LAYOUT_ID, &magic, sizeof(magic));
	}
#endif /* defined(CONFIG_NVS) */

	if (err < 0) {
		printk("Cannot initialize storage!\n");
	}

	return err;
}

int ndef_file_update(uint8_t const *buff, uint32_t size)
{
	/* Update FLASH file with new NDEF message. */
#if defined(CONFIG_NVS)
	return nvs_write(&fs, FLASH_URL_ADDRESS_ID, buff, size);
#elif defined(CONFIG_ZMS)
	return zms_write(&fs, FLASH_URL_ADDRESS_ID, buff, size);
#endif /* defined(CONFIG_NVS) */
}

/** .. include_startingpoint_ndef_file_rst */
int ndef_file_default_message(uint8_t *buff, uint32_t *size)
{
	int err;
	uint32_t ndef_size = nfc_t4t_ndef_file_msg_size_get(*size);

	/* Encode URI message into buffer. */
	err = nfc_ndef_uri_msg_encode(NFC_URI_HTTP_WWW,
				      m_url,
				      sizeof(m_url),
				      nfc_t4t_ndef_file_msg_get(buff),
				      &ndef_size);
	if (err) {
		return err;
	}

	err = nfc_t4t_ndef_file_encode(buff, &ndef_size);
	if (err) {
		return err;
	}

	*size = ndef_size;

	return 0;
}
/** .. include_endpoint_ndef_file_rst */

int ndef_restore_default(uint8_t *buff, uint32_t size)
{
	int err;

	err = ndef_file_default_message(buff, &size);
	if (err < 0) {
		printk("Cannot create default message!\n");
		return err;
	}

	/* Save record with default NDEF message. */
	err = ndef_file_update(buff, size);
	if (err < 0) {
		printk("Cannot flash NDEF message!\n");
	}
	return err;
}

int ndef_file_load(uint8_t *buff, uint32_t size)
{
	int err;

	/* If there is no record with given ID, create default message and store
	 * in FLASH. FLASH_URL_ADDRESS_ID is used to store an address, lets see
	 * if we can read it from flash, since we don't know the size read the
	 * maximum possible
	 */
#if defined(CONFIG_NVS)
	err = nvs_read(&fs, FLASH_URL_ADDRESS_ID, buff, size);
#elif defined(CONFIG_ZMS)
	err = zms_read(&fs, FLASH_URL_ADDRESS_ID, buff, size);
#endif /* defined(CONFIG_NVS) */

	if (err > 0) { /* Item was found, show it */
		printk("Found NDEF file record.\n");
	} else {
		printk("NDEF file record not found, creating default NDEF.\n");
		/* Create default NDEF message. */
		err = ndef_restore_default(buff, size);
	}

	return err;
}

#if defined(CONFIG_ZMS)
int content_store_save_image(const uint8_t *img, size_t len, uint8_t fmt)
{
	struct cs_header hdr = {
		.magic = CS_MAGIC,
		.kind = CONTENT_STORE_KIND_IMAGE,
		.fmt = fmt,
		.len = (uint16_t)len,
		.crc = xepr_crc32(img, len),
	};

	if (img == NULL || len == 0 || len > CS_CHUNK_LEN * 2) {
		return -EINVAL;
	}

	/* Chunks first, metadata last: the metadata entry is the commit
	 * point, so an interrupted save leaves the previous content intact.
	 */
	if (zms_write(&fs, CS_IMG0_ID, img, MIN(len, CS_CHUNK_LEN)) < 0) {
		return -EIO;
	}
	if (len > CS_CHUNK_LEN) {
		if (zms_write(&fs, CS_IMG1_ID, img + CS_CHUNK_LEN,
			      len - CS_CHUNK_LEN) < 0) {
			return -EIO;
		}
	} else {
		zms_delete(&fs, CS_IMG1_ID); /* drop a stale second chunk */
	}

	return zms_write(&fs, CS_META_ID, &hdr, sizeof(hdr)) < 0 ? -EIO : 0;
}

int content_store_save_text(const char *text)
{
	struct cs_header hdr = {
		.magic = CS_MAGIC,
		.kind = CONTENT_STORE_KIND_TEXT,
	};
	uint8_t buf[sizeof(struct cs_header) + CS_TEXT_MAX];

	if (text == NULL) {
		return -EINVAL;
	}
	hdr.len = strnlen(text, CS_TEXT_MAX - 1) + 1; /* incl. NUL */

	/* The image entries are left behind: kind == TEXT means they are
	 * ignored on load (and overwritten by the next image save).
	 */
	memcpy(buf, &hdr, sizeof(hdr));
	memcpy(buf + sizeof(hdr), text, hdr.len);

	return zms_write(&fs, CS_META_ID, buf, sizeof(hdr) + hdr.len) < 0
		       ? -EIO
		       : 0;
}

int content_store_load(uint8_t *img, size_t img_size, uint8_t *fmt,
		       char *text, size_t text_size)
{
	struct cs_header hdr;

	if (zms_read(&fs, CS_META_ID, &hdr, sizeof(hdr)) !=
		    (ssize_t)sizeof(hdr) ||
	    hdr.magic != CS_MAGIC) {
		return -ENOENT;
	}

	if (hdr.kind == CONTENT_STORE_KIND_TEXT) {
		uint8_t buf[sizeof(struct cs_header) + CS_TEXT_MAX];

		if (hdr.len == 0 || hdr.len > CS_TEXT_MAX ||
		    hdr.len > text_size ||
		    zms_read(&fs, CS_META_ID, buf,
			     sizeof(hdr) + hdr.len) !=
			    (ssize_t)(sizeof(hdr) + hdr.len)) {
			return -ENOENT;
		}
		memcpy(text, buf + sizeof(hdr), hdr.len);
		return CONTENT_STORE_KIND_TEXT;
	}

	if (hdr.kind != CONTENT_STORE_KIND_IMAGE || hdr.len == 0 ||
	    hdr.len > img_size || hdr.len > CS_CHUNK_LEN * 2) {
		return -ENOENT;
	}

	/* Read the chunks at their exact stored length, then verify the
	 * CRC over the reassembled image.
	 */
	size_t first = MIN(hdr.len, CS_CHUNK_LEN);

	if (zms_get_data_length(&fs, CS_IMG0_ID) != (ssize_t)first ||
	    zms_read(&fs, CS_IMG0_ID, img, first) != (ssize_t)first) {
		return -ENOENT;
	}
	if (hdr.len > CS_CHUNK_LEN) {
		size_t rest = hdr.len - CS_CHUNK_LEN;

		if (zms_get_data_length(&fs, CS_IMG1_ID) != (ssize_t)rest ||
		    zms_read(&fs, CS_IMG1_ID, img + CS_CHUNK_LEN,
			     rest) != (ssize_t)rest) {
			return -ENOENT;
		}
	}
	if (xepr_crc32(img, hdr.len) != hdr.crc) {
		return -ENOENT;
	}

	*fmt = hdr.fmt;
	return CONTENT_STORE_KIND_IMAGE;
}
#else
int content_store_save_image(const uint8_t *img, size_t len, uint8_t fmt)
{
	ARG_UNUSED(img);
	ARG_UNUSED(len);
	ARG_UNUSED(fmt);
	return -ENOSYS;
}

int content_store_save_text(const char *text)
{
	ARG_UNUSED(text);
	return -ENOSYS;
}

int content_store_load(uint8_t *img, size_t img_size, uint8_t *fmt,
		       char *text, size_t text_size)
{
	ARG_UNUSED(img);
	ARG_UNUSED(img_size);
	ARG_UNUSED(fmt);
	ARG_UNUSED(text);
	ARG_UNUSED(text_size);
	return -ENOSYS;
}
#endif /* defined(CONFIG_ZMS) */

/** @} */
