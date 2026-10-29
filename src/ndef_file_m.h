/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _NDEF_FILE_M_H__
#define _NDEF_FILE_M_H__

/** @file
 *
 * @defgroup nfc_writable_ndef_msg_example_ndef_file_m ndef_file_m.h
 * @{
 * @ingroup nfc_writable_ndef_msg_example
 * @brief FLASH service for the NFC writable NDEF message example.
 *
 */

#include <stddef.h>
#include <zephyr/types.h>

/**
 * @brief   Function for initializing the storage module.
 *
 * @return 0 when module has been set up properly,
 * error code otherwise.
 */
int ndef_file_setup(void);

/**
 * @brief   Function for updating NDEF message in the flash file.
 *
 * @details Storage update operation is performed on main loop.
 *
 * @param buff Pointer to the NDEF message to be stored in flash.
 * @param size Size of NDEF message.
 *
 * @return  0 when update request has been added to the queue.
 *          Otherwise, error code.
 */
int ndef_file_update(uint8_t const *buff, uint32_t size);

/**
 * @brief Function for loading NDEF message from the flash file.
 *
 * @details If the record does not exist, the default NDEF message
 * is created and stored in flash.
 *
 * @param buff Pointer to the buffer for NDEF message.
 * @param size Size of the buffer.
 *
 * @return  0 when NDEF message has been retrieved properly. Otherwise error
 * code is returned.
 */
int ndef_file_load(uint8_t *buff, uint32_t size);

/**
 * @brief Function for creating the default NDEF message: URL "nordicsemi.com".
 *
 * @param buff Pointer to the NDEF message buffer.
 * @param size Pointer to the variable holding buffer size as input, size
 * of the created message as output.
 *
 * @return 0 if the message has been created, error code otherwise.
 */
int ndef_file_default_message(uint8_t *buff, uint32_t *size);

/**
 * @brief Function for creating and storing the default NDEF message:
 * URL "nordicsemi.com".
 *
 * @param buff Pointer to the NDEF message buffer.
 * @param size Pointer to the variable holding buffer size as input, size
 * of the created message as output.
 *
 * @return 0 if the message has been created, error code otherwise.
 */
int ndef_restore_default(uint8_t *buff, uint32_t size);

/**
 * @brief Kind of content stored by the persistent content store
 *        (content_store_save_*() / content_store_load()).
 */
#define CONTENT_STORE_KIND_IMAGE 1
#define CONTENT_STORE_KIND_TEXT  2

/**
 * @brief Persist the displayed image so it survives a reboot.
 *
 * @details The NDEF file is stored as a single ZMS entry, so payloads
 * above one sector (e.g. the hex text encoding) cannot be persisted
 * that way. The content store splits the decoded image into entries of
 * at most 4000 bytes; the metadata entry is written last and acts as
 * the commit point.
 *
 * @param img Decoded image (XEPR format layout).
 * @param len Image length in bytes (XEPR_IMAGE_LEN / XEPR4C_IMAGE_LEN).
 * @param fmt XEPR_FORMAT_* of the image.
 *
 * @return 0 on success, negative error code otherwise.
 */
int content_store_save_image(const uint8_t *img, size_t len, uint8_t fmt);

/**
 * @brief Persist the displayed text so it survives a reboot.
 *
 * @param text NUL-terminated text (truncated to 255 characters).
 *
 * @return 0 on success, negative error code otherwise.
 */
int content_store_save_text(const char *text);

/**
 * @brief Load the content saved by content_store_save_*().
 *
 * @param[out] img       Output buffer for the image (XEPR_FB_MAX_LEN bytes).
 * @param[in]  img_size  Size of the image buffer.
 * @param[out] fmt       Set to the XEPR_FORMAT_* of the loaded image.
 * @param[out] text      Output buffer for the text (256 bytes).
 * @param[in]  text_size Size of the text buffer.
 *
 * @retval CONTENT_STORE_KIND_IMAGE Image loaded into @p img (and @p fmt).
 * @retval CONTENT_STORE_KIND_TEXT  Text loaded into @p text.
 * @retval -ENOENT Nothing valid stored (first boot or corruption).
 */
int content_store_load(uint8_t *img, size_t img_size, uint8_t *fmt,
		       char *text, size_t text_size);

/** @} */

#endif /* _NDEF_FILE_M_H__ */
