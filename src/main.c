/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief XIAO nRF54LM20A + 2.13" ePaper: NFC writable NDEF + welcome screen.
 *
 * V0.5: NDEF messages can carry a full image (XEPR protocol) which is
 * rendered directly to the ePaper framebuffer, or plain text shown via LVGL.
 * Every screen change ends in exactly one full refresh (no partial refresh,
 * no discarded NFC updates) so text and images always display.
 * V0.6: XEPR frames are repacked into the driver's VTILED layout (the same
 * layout the LVGL mono path emits), so images render correctly.
 * V0.7: panel polarity fixed — this panel's RAM is 1 = white (0 = black),
 * so the image repack sets bits for white pixels and the LVGL path uses
 * CONFIG_LV_Z_COLOR_MONO_HW_INVERSION.
 * V0.8: support for the 2.13" Quadruple Color panel (GDEY0213F51 /
 * JD79661): XEPR format 2 images (2bpp, 8000 bytes) are written to the
 * panel RAM directly via the jd79661 app driver. Selected at build time
 * with -DDTC_OVERLAY_FILE=boards/quadcolor.overlay.
 * V0.9: 4C images can also arrive as two split Text records ("XEPR2A|"/
 * "XEPR2B|", ~8k chars each, one NFC write per half) for writer apps
 * without binary MIME support; the halves are assembled and CRC-checked.
 * V0.10: the last-written image or text is persisted in a dedicated
 * content store (split ZMS entries, metadata written last) and restored
 * on boot, so content survives a reboot even when the NDEF file itself
 * is too large for a single ZMS entry.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <nfc_t4t_lib.h>

#include <nfc/ndef/msg.h>
#include <nfc/t4t/ndef_file.h>

#include <lvgl.h>

#include "ndef_file_m.h"
#include "ndef_parser.h"
#include "xepr.h"

/* Quad-color panel build (boards/quadcolor.overlay): display_image takes
 * the JD79661 2bpp path; otherwise the BW SSD1680 path is used.
 */
#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_display), jd79661_gdey0213f51)
#define PANEL_IS_JD79661 1
#include "jd79661.h"
#else
#define PANEL_IS_JD79661 0
#endif

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

/** Max text length shown on the ePaper (longer text is truncated). */
#define NDEF_TEXT_MAX_LEN 256

/** Rows of the framebuffer addressable through the display API (must be
 *  a multiple of 8 and is capped by the driver's page model at 120).
 */
#define XEPR_FB_ROWS_WRITABLE 120

static uint8_t ndef_msg_buf[CONFIG_NDEF_FILE_SIZE]; /**< Buffer for NDEF file. */
static size_t ndef_file_len; /**< Length of the NDEF file loaded from flash. */

/* Latest NDEF file received over NFC. The callback always accepts it
 * (latest wins, nothing is ever discarded); the worker copies it out under
 * the same mutex before processing.
 */
static K_MUTEX_DEFINE(flash_buf_lock);
static uint8_t flash_buf[CONFIG_NDEF_FILE_SIZE]; /**< Buffer for flash update. */
static size_t flash_buf_len; /**< Length of the flash buffer. */

static K_SEM_DEFINE(nfc_write_sem, 0, 1);

/* Text extracted from the last written NDEF, handed over to the LVGL thread. */
K_MSGQ_DEFINE(ndef_text_q, NDEF_TEXT_MAX_LEN, 4, 1);

/* Decoded XEPR image, shared between the NFC worker and the display thread.
 * Sized for the largest format (4C: 8000 bytes).
 */
static uint8_t xepr_fb[XEPR_FB_MAX_LEN];
static K_MUTEX_DEFINE(xepr_fb_lock);
/* Format of the image currently in xepr_fb (XEPR_FORMAT_*), written under
 * xepr_fb_lock together with the image.
 */
static uint8_t xepr_fmt = XEPR_FORMAT_BW1;
/* Token handed to the display thread when xepr_fb was updated. */
K_MSGQ_DEFINE(xepr_image_q, sizeof(int), 2, 1);

/* NFC worker thread: writes updated NDEF to flash, parses it and queues
 * the text or image for display (never runs in NFC callback).
 */
#define NFC_WORKER_STACK_SIZE 4096
#define NFC_WORKER_PRIORITY   5

/* XEPR image format this build can display. */
#if PANEL_IS_JD79661
#define PANEL_XEPR_FORMAT XEPR_FORMAT_4C2
#else
#define PANEL_XEPR_FORMAT XEPR_FORMAT_BW1
#endif

static void nfc_worker(void)
{
	static char text[NDEF_TEXT_MAX_LEN];
	/* Snapshot of the NDEF file, copied under flash_buf_lock so a
	 * concurrent NFC update can never corrupt or discard it.
	 */
	static uint8_t file[CONFIG_NDEF_FILE_SIZE];
	size_t file_len;

	while (true) {
		k_sem_take(&nfc_write_sem, K_FOREVER);

		k_mutex_lock(&flash_buf_lock, K_FOREVER);
		file_len = flash_buf_len;
		memcpy(file, flash_buf, file_len);
		k_mutex_unlock(&flash_buf_lock);

		if (ndef_file_update(file, file_len) < 0) {
			/* Payloads above the ZMS sector size (e.g.
			 * the hex text encoding) cannot be made
			 * persistent, but they are still displayed.
			 */
			LOG_WRN("NDEF not stored to flash "
				"(%u bytes, too large?)", file_len);
		} else {
			LOG_INF("NDEF message successfully flashed.");
		}

		/* The file holds a T4T NDEF file: 2-byte NLEN prefix
		 * followed by the NDEF message.
		 */
		if (file_len > NFC_NDEF_FILE_NLEN_FIELD_SIZE) {
			const uint8_t *msg =
				file + NFC_NDEF_FILE_NLEN_FIELD_SIZE;
			size_t msg_len = file_len -
				NFC_NDEF_FILE_NLEN_FIELD_SIZE;

			k_mutex_lock(&xepr_fb_lock, K_FOREVER);
			int img_err = xepr_from_ndef(msg, msg_len, xepr_fb,
						      sizeof(xepr_fb),
						      &xepr_fmt);
			k_mutex_unlock(&xepr_fb_lock);

			/* Reject images encoded for the other panel type
			 * (e.g. a BW frame on the quad-color build): they
			 * would render as noise.
			 */
			if (img_err == 0 && xepr_fmt != PANEL_XEPR_FORMAT) {
				LOG_WRN("XEPR format %u not for this panel",
					xepr_fmt);
				img_err = -EINVAL;
			}

			if (img_err == 0) {
				LOG_INF("XEPR image received "
					"(%u-byte NDEF file)", file_len);

				/* Persist the decoded image so it survives a
				 * reboot (the NDEF file itself may be too
				 * large for one ZMS entry). xepr_fb is only
				 * written by this thread, so reading it here
				 * without the lock is safe.
				 */
				size_t img_len = xepr_fmt == XEPR_FORMAT_4C2
							 ? XEPR4C_IMAGE_LEN
							 : XEPR_IMAGE_LEN;

				if (content_store_save_image(xepr_fb, img_len,
							     xepr_fmt) < 0) {
					LOG_WRN("Image not persisted to flash");
				} else {
					LOG_INF("Image persisted to flash");
				}

				int token = 1;
				if (k_msgq_put(&xepr_image_q, &token,
					       K_NO_WAIT) != 0) {
					LOG_WRN("Image queue full, dropped");
				}
			} else if (img_err == XEPR_ERR_WAIT) {
				/* One half of a split 4C text image arrived;
				 * nothing to display until the other half.
				 */
				LOG_INF("XEPR2 half received, "
					"waiting for the other half");
			} else if (img_err == -EINVAL) {
				LOG_WRN("Invalid XEPR frame "
					"(bad format or CRC)");
			} else {
				int err = ndef_text_parse(
					msg, msg_len, text,
					sizeof(text));

				if (err == 0) {
					LOG_INF("NDEF text: \"%s\"",
						text);

					/* Persist the text so it survives a
					 * reboot.
					 */
					if (content_store_save_text(text) < 0) {
						LOG_WRN("Text not persisted to flash");
					}

					if (k_msgq_put(&ndef_text_q, text,
						       K_NO_WAIT) != 0) {
						LOG_WRN("Text queue full, "
							"dropped");
					}
				} else {
					LOG_WRN("No Text record in "
						"NDEF (err: %d)", err);
				}
			}
		}
	}
}

K_THREAD_DEFINE(nfc_worker_tid, NFC_WORKER_STACK_SIZE, nfc_worker,
		NULL, NULL, NULL, NFC_WORKER_PRIORITY, 0, 0);

static void flash_buffer_prepare(size_t data_length)
{
	size_t file_len = MIN(data_length + NFC_NDEF_FILE_NLEN_FIELD_SIZE,
			      sizeof(flash_buf));

	/* Always accept the latest NDEF (latest wins). The worker copies the
	 * buffer out under the same mutex, so nothing is ever discarded and
	 * the data the worker sees is always consistent.
	 */
	k_mutex_lock(&flash_buf_lock, K_FOREVER);
	flash_buf_len = file_len;
	memcpy(flash_buf, ndef_msg_buf, file_len);
	k_mutex_unlock(&flash_buf_lock);

	k_sem_give(&nfc_write_sem);
}

/**
 * @brief Callback function for handling NFC events.
 */
static void nfc_callback(void *context,
			 nfc_t4t_event_t event,
			 const uint8_t *data,
			 size_t data_length,
			 uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(flags);

	switch (event) {
	case NFC_T4T_EVENT_FIELD_ON:
		LOG_INF("NFC field on");
		break;

	case NFC_T4T_EVENT_FIELD_OFF:
		LOG_INF("NFC field off");
		break;

	case NFC_T4T_EVENT_NDEF_READ:
		LOG_INF("NDEF read");
		break;

	case NFC_T4T_EVENT_NDEF_UPDATED:
		if (data_length > 0) {
			LOG_INF("NDEF updated (%zu bytes)", data_length);
			flash_buffer_prepare(data_length);
		}
		break;

	default:
		break;
	}
}

static int nfc_init(void)
{
	/* Initialize storage. */
	if (ndef_file_setup() < 0) {
		LOG_ERR("Cannot setup NDEF file!");
		return -EIO;
	}
	/* Load NDEF message from the flash file. */
	int len = ndef_file_load(ndef_msg_buf, sizeof(ndef_msg_buf));

	if (len < 0) {
		LOG_ERR("Cannot load NDEF file!");
		return -EIO;
	}
	ndef_file_len = len;

	/* Set up NFC */
	int err = nfc_t4t_setup(nfc_callback, NULL);

	if (err < 0) {
		LOG_ERR("Cannot setup t4t library!");
		return err;
	}
	/* Run Read-Write mode for Type 4 Tag platform */
	if (nfc_t4t_ndef_rwpayload_set(ndef_msg_buf,
					sizeof(ndef_msg_buf)) < 0) {
		LOG_ERR("Cannot set payload!");
		return -EIO;
	}
	/* Start sensing NFC field */
	if (nfc_t4t_emulation_start() < 0) {
		LOG_ERR("Cannot start emulation!");
		return -EIO;
	}

	LOG_INF("NFC started");
	return 0;
}

/* Welcome screen: shown until the first NDEF update arrives. */
static lv_obj_t *title_label;
static lv_obj_t *status_label;
static lv_obj_t *footer_label;
static lv_obj_t *ndef_text_label;

static void welcome_screen_init(void)
{
	/* White background, no padding / scrollbar. */
	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(scr, 0, LV_STATE_DEFAULT);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	lv_disp_t *disp = lv_disp_get_default();
	lv_coord_t width = lv_disp_get_hor_res(disp);
	lv_coord_t height = lv_disp_get_ver_res(disp);
	LOG_INF("Display: %d x %d", width, height);

	/* Title */
	title_label = lv_label_create(scr);
	lv_label_set_text(title_label, "NFC ePaper Demo");
	lv_obj_set_style_text_color(title_label, lv_color_black(), LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, LV_STATE_DEFAULT);
	lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -25);

	/* Status */
	status_label = lv_label_create(scr);
	lv_label_set_text(status_label, "Ready for NFC");
	lv_obj_set_style_text_color(status_label, lv_color_black(), LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, LV_STATE_DEFAULT);
	lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 15);

	/* Footer */
	footer_label = lv_label_create(scr);
	lv_label_set_text(footer_label, "Waiting for image...");
	lv_obj_set_style_text_color(footer_label, lv_color_black(), LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
	lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -5);

	/* Label for NDEF text, hidden until the first update arrives. */
	ndef_text_label = lv_label_create(scr);
	lv_obj_set_style_text_color(ndef_text_label, lv_color_black(), LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(ndef_text_label, &lv_font_montserrat_24, LV_STATE_DEFAULT);
	lv_label_set_long_mode(ndef_text_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(ndef_text_label, width - 20);
	lv_obj_align(ndef_text_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(ndef_text_label, LV_OBJ_FLAG_HIDDEN);
}

/* Replace the welcome screen with the received NDEF text. */
static void display_ndef_text(const char *text)
{
	lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(footer_label, LV_OBJ_FLAG_HIDDEN);

	lv_label_set_text(ndef_text_label, text);
	lv_obj_clear_flag(ndef_text_label, LV_OBJ_FLAG_HIDDEN);

	LOG_INF("Displaying NDEF text");
}

static const struct device *display_dev;

/**
 * @brief Render the XEPR framebuffer directly to the panel.
 *
 * Bypasses LVGL (and pauses it while an image is shown). The two panel
 * builds take different paths:
 *
 * - Quad-color (JD79661): the XEPR format 2 payload is the panel RAM
 *   image (2bpp, 8000 bytes) and is displayed as-is; the driver performs
 *   the full init/write/refresh cycle and retries on transient failure.
 * - BW (SSD1680): the XEPR frame is row-major, MSB-first, 1 = white; the
 *   SSD16xx driver expects the layout the LVGL mono path produces (see
 *   lvgl_display_mono.c): VTILED — one byte per column covering 8 rows,
 *   MSB = first row. The panel RAM polarity is 1 = white, 0 = black
 *   (matching the XEPR encoding), so white pixels set the bit — the same
 *   inversion CONFIG_LV_Z_COLOR_MONO_HW_INVERSION applies on the LVGL
 *   path. blanking on/off brackets the write so the driver performs one
 *   full refresh.
 */
#if PANEL_IS_JD79661
static int display_image(void)
{
	static uint8_t render[XEPR4C_IMAGE_LEN];
	int err = -EIO;

	k_mutex_lock(&xepr_fb_lock, K_FOREVER);
	memcpy(render, xepr_fb, sizeof(render));
	k_mutex_unlock(&xepr_fb_lock);

	/* The driver itself retries once on a transient bus failure, so a
	 * failure here is final (logged, image kept for the next update).
	 */
	err = jd79661_display_frame(display_dev, render);

	LOG_INF("Image displayed (%d)", err);
	return err;
}
#else
static int display_image(void)
{
	static uint8_t render[XEPR_IMAGE_LEN];
	static uint8_t vbuf[XEPR_FB_ROW_BYTES * XEPR_FB_ROWS_WRITABLE];
	int err = -EIO;

	k_mutex_lock(&xepr_fb_lock, K_FOREVER);
	memcpy(render, xepr_fb, sizeof(render));
	k_mutex_unlock(&xepr_fb_lock);

	memset(vbuf, 0x00, sizeof(vbuf)); /* all bits clear = all black */
	for (uint32_t row = 0; row < XEPR_FB_ROWS_WRITABLE; row++) {
		for (uint32_t col = 0; col < XEPR_FB_W; col++) {
			const bool white =
				(render[row * XEPR_FB_ROW_BYTES + col / 8] >>
				 (7 - col % 8)) & 1U;

			if (white) { /* white pixel: set its bit (1 = white) */
				vbuf[col + (row / 8) * XEPR_FB_W] |=
					BIT(7 - row % 8);
			}
		}
	}

	const struct display_buffer_descriptor desc = {
		.buf_size = XEPR_FB_ROW_BYTES * XEPR_FB_ROWS_WRITABLE,
		.width = XEPR_FB_W,
		.height = XEPR_FB_ROWS_WRITABLE,
		.pitch = XEPR_FB_W,
	};

	/* Suppress the automatic update during the write, then trigger
	 * one full refresh when unblanking. Retry once on error so a
	 * transient bus failure cannot silently swallow the image.
	 */
	for (int attempt = 0; attempt < 2 && err != 0; attempt++) {
		err = display_blanking_on(display_dev);
		if (err == 0) {
			err = display_write(display_dev, 0, 0, &desc, vbuf);
		}
		/* Unblank regardless: this triggers the full refresh. */
		if (display_blanking_off(display_dev) != 0 && err == 0) {
			err = -EIO;
		}
		if (err != 0) {
			k_msleep(50);
		}
	}

	LOG_INF("Image displayed (%d)", err);
	return err;
}
#endif /* PANEL_IS_JD79661 */

static int display_init(void)
{
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready!");
		return -ENODEV;
	}
	LOG_INF("Display device ready.");

	lv_init();

	/* Keep the panel blanked until the first frame has been rendered:
	 * with blanking on, display_write only fills the RAM; exactly one
	 * full refresh then happens when the main loop unblankes after the
	 * first (bracketed) LVGL render. This rules out any uncontrolled
	 * refresh during startup.
	 */
	if (display_blanking_on(display_dev)) {
		LOG_ERR("Failed to turn on display blanking!");
		return -EIO;
	}

	welcome_screen_init();
	return 0;
}

/**
 * @brief   Function for application main entry.
 */
int main(void)
{
	char stored_text[NDEF_TEXT_MAX_LEN];
	bool has_stored_image = false;
	bool has_stored_text = false;
	bool img_mode = false;
	/* Set whenever the LVGL screen changed and the next lv_task_handler()
	 * call must be bracketed with blanking on/off so the change ends in
	 * exactly one full refresh. True initially for the welcome screen.
	 */
	bool text_dirty = true;

	/* Start NFC first: this loads the stored NDEF from flash, so we
	 * know whether to restore the last image or text on the display.
	 */
	if (nfc_init() < 0) {
		goto fail;
	}

	/* Prefer the persistent content store (V0.10+); fall back to the
	 * NDEF-based restore for content written by older firmware.
	 */
	int stored = content_store_load(xepr_fb, sizeof(xepr_fb), &xepr_fmt,
					stored_text, sizeof(stored_text));

	if (stored == CONTENT_STORE_KIND_IMAGE) {
		has_stored_image = xepr_fmt == PANEL_XEPR_FORMAT;
	} else if (stored == CONTENT_STORE_KIND_TEXT) {
		has_stored_text = true;
	} else if (ndef_file_len > NFC_NDEF_FILE_NLEN_FIELD_SIZE) {
		int img_err;

		k_mutex_lock(&xepr_fb_lock, K_FOREVER);
		img_err = xepr_from_ndef(
			ndef_msg_buf + NFC_NDEF_FILE_NLEN_FIELD_SIZE,
			ndef_file_len - NFC_NDEF_FILE_NLEN_FIELD_SIZE,
			xepr_fb, sizeof(xepr_fb), &xepr_fmt);
		has_stored_image = img_err == 0 &&
			xepr_fmt == PANEL_XEPR_FORMAT;
		k_mutex_unlock(&xepr_fb_lock);

		/* img_err == XEPR_ERR_WAIT (a lone split-4C half) restores
		 * neither an image nor text: skip the text fallback for it.
		 */
		if (img_err != 0 && img_err != XEPR_ERR_WAIT) {
			has_stored_text = ndef_text_parse(
				ndef_msg_buf + NFC_NDEF_FILE_NLEN_FIELD_SIZE,
				ndef_file_len - NFC_NDEF_FILE_NLEN_FIELD_SIZE,
				stored_text, sizeof(stored_text)) == 0;
		}
	}

	if (display_init() < 0) {
		goto fail;
	}

	if (has_stored_image) {
		/* Restore the image written by the last NFC update. */
		LOG_INF("Restored XEPR image");
		display_image();
		img_mode = true;
	} else if (has_stored_text) {
		/* Restore the text written by the last NFC update. */
		LOG_INF("Restored NDEF text: \"%s\"", stored_text);
		display_ndef_text(stored_text);
	}

#if PANEL_IS_JD79661
	LOG_INF("Starting NFC ePaper (V0.10: quad-color, persistent)");
#else
	LOG_INF("Starting NFC ePaper (V0.10: BW, persistent)");
#endif

	while (true) {
		char text[NDEF_TEXT_MAX_LEN];
		int token;

		if (k_msgq_get(&xepr_image_q, &token, K_NO_WAIT) == 0) {
			display_image();
			img_mode = true;
		}

		if (k_msgq_get(&ndef_text_q, text, K_NO_WAIT) == 0) {
			display_ndef_text(text);
			if (img_mode) {
				/* LVGL was paused while the image was shown:
				 * force a full redraw of the screen.
				 */
				lv_obj_invalidate(lv_scr_act());
				img_mode = false;
			}
			text_dirty = true;
		}

		/* LVGL objects are not thread-safe: the worker queues the
		 * text, only this thread touches the label. While an image
		 * is shown LVGL is paused so it cannot overwrite the panel.
		 */
		if (!img_mode) {
			if (text_dirty) {
				/* Bracket the whole render with blanking
				 * on/off: the writes only fill the RAM and
				 * unblanking triggers exactly one full
				 * refresh (strong, deterministic).
				 */
				display_blanking_on(display_dev);
				lv_task_handler();
				if (display_blanking_off(display_dev)) {
					LOG_WRN("Full refresh failed");
				}
				text_dirty = false;
			} else {
				lv_task_handler();
			}
		}
		k_sleep(K_MSEC(200));
	}

fail:
	#if CONFIG_REBOOT
		sys_reboot(SYS_REBOOT_COLD);
	#endif /* CONFIG_REBOOT */
	return -EIO;
}
