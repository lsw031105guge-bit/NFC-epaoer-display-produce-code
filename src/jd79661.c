/*
 * Good Display GDEY0213F51 (Seeed 2.13" Quadruple Color ePaper, 122x250)
 * display driver. Controller: JD79661 (JD79661AA).
 *
 * The controller RAM is a single 2bpp plane: 250 gate lines x 128 sources,
 * 4 sources per byte (source 0 in bits [7:6], source 1 in [5:4], ...),
 * 8000 bytes in total, written with command 0x10. Color codes: 0x00 black,
 * 0x01 white, 0x02 yellow, 0x03 red. BUSY is LOW while the controller is
 * busy. After every refresh the controller must be re-initialized, so each
 * update performs the full cycle: hardware reset, register init, power on,
 * RAM write, refresh (~21-25 s), power off, deep sleep.
 *
 * The panel is driven in landscape orientation like the BW GDEY0213B74:
 * the display API exposes a 250x122 MONO01 screen where screen x maps to
 * gate line 0..249 and screen y to source 0..121 (bit set = white, which
 * matches CONFIG_LV_Z_COLOR_MONO_HW_INVERSION). XEPR format 2 images
 * bypass the mono conversion and supply the panel RAM contents directly.
 *
 * Sequences follow the GxEPD2 reference driver (GxEPD2_213c_GDEY0213F51)
 * and the Seeed quadruple-color demo for the ePaper Driver Board.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/kernel.h>

#include "jd79661.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(jd79661);

/* Reset / busy timings. */
#define JD79661_RESET_LOW_MS   40
#define JD79661_RESET_HIGH_MS  50
#define JD79661_BUSY_POLL_MS   10
#define JD79661_BUSY_TIMEOUT_RESET_MS   1000
#define JD79661_BUSY_TIMEOUT_POWER_ON_MS 500
#define JD79661_BUSY_TIMEOUT_REFRESH_MS 26000
#define JD79661_BUSY_TIMEOUT_POWER_OFF_MS 300

/* JD79661 commands. */
#define JD79661_CMD_CMD_SETTING    0x00 /* PSR */
#define JD79661_CMD_PWR_SETTING    0x01
#define JD79661_CMD_PWR_OFF_SETTING 0x03
#define JD79661_CMD_BOOSTER_SOFT_START 0x06
#define JD79661_CMD_DEEP_SLEEP     0x07
#define JD79661_CMD_PWR_ON         0x04
#define JD79661_CMD_PWR_OFF        0x02
#define JD79661_CMD_DISPLAY_REFRESH 0x12
#define JD79661_CMD_RAM_WRITE      0x10
#define JD79661_CMD_VCOM_DATA_INTERVAL 0x50
#define JD79661_CMD_TCON           0x60
#define JD79661_CMD_TRES           0x61
#define JD79661_CMD_GATE_SOURCE_SETTING 0xE7
#define JD79661_CMD_GATE_DRIVING_VOLTAGE 0xE3
#define JD79661_CMD_SOURCE_DRIVING_VOLTAGE_CTRL 0xB4
#define JD79661_CMD_GATE_SOURCE_VOLTAGE_CTRL 0xB5
#define JD79661_CMD_SOURCE_VOLTAGE_CTRL 0xE9
#define JD79661_CMD_PLL           0x30
#define JD79661_CMD_CDI_CMD       0x4D

struct jd79661_config {
	const struct device *mipi_dev;
	struct mipi_dbi_config dbi_config;
	struct gpio_dt_spec busy_gpio;
	uint16_t width;  /* visible width (landscape, <= 250 gates) */
	uint16_t height; /* visible height (landscape, <= 122 sources) */
};

struct jd79661_data {
	bool blanking;
	/* Panel RAM shadow, 250 gate lines x 32 bytes (2bpp). */
	uint8_t ram[JD79661_RAM_BYTES];
};

static const struct display_driver_api jd79661_driver_api;

static int jd79661_cmd(const struct device *dev, uint8_t cmd,
		       const uint8_t *data, size_t len)
{
	const struct jd79661_config *config = dev->config;
	int err = mipi_dbi_command_write(config->mipi_dev, &config->dbi_config,
					 cmd, data, len);

	mipi_dbi_release(config->mipi_dev, &config->dbi_config);
	return err;
}

/* BUSY is LOW while the controller is busy (opposite of the SSD1680). */
static int jd79661_busy_wait(const struct device *dev, uint32_t timeout_ms)
{
	const struct jd79661_config *config = dev->config;
	uint32_t waited = 0;

	while (gpio_pin_get_dt(&config->busy_gpio) == 0) {
		if (waited >= timeout_ms) {
			LOG_ERR("BUSY timeout after %u ms (cmd cycle)", timeout_ms);
			return -ETIMEDOUT;
		}
		k_msleep(JD79661_BUSY_POLL_MS);
		waited += JD79661_BUSY_POLL_MS;
	}
	return 0;
}

/* Register init sequence (GxEPD2_213c_GDEY0213F51 / Seeed demo). */
static int jd79661_init_registers(const struct device *dev)
{
	static const struct {
		uint8_t cmd;
		uint8_t data[8];
		uint8_t len;
	} seq[] = {
		{ 0x4D, { 0x78 }, 1 },
		{ 0x00, { 0x0F, 0x29 }, 2 },      /* PSR */
		{ 0x01, { 0x07, 0x00 }, 2 },      /* PWRR */
		{ 0x03, { 0x10, 0x54, 0x44 }, 3 },/* POFS */
		{ 0x06, { 0x05, 0x00, 0x3F, 0x0A, 0x25, 0x12, 0x1A }, 7 },
		{ 0x50, { 0x37 }, 1 },            /* CDI */
		{ 0x60, { 0x02, 0x02 }, 2 },      /* TCON */
		{ 0x61, { 0x00, 0x80, 0x00, 0xFA }, 4 }, /* TRES: 128 x 250 */
		{ 0xE7, { 0x1C }, 1 },
		{ 0xE3, { 0x22 }, 1 },
		{ 0xB4, { 0xD0 }, 1 },
		{ 0xB5, { 0x03 }, 1 },
		{ 0xE9, { 0x01 }, 1 },
		{ 0x30, { 0x08 }, 1 },            /* PLL */
	};
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(seq); i++) {
		err = jd79661_cmd(dev, seq[i].cmd, seq[i].data, seq[i].len);
		if (err < 0) {
			LOG_ERR("init cmd 0x%02x failed: %d", seq[i].cmd, err);
			return err;
		}
	}

	/* Power on and wait for the controller to become ready. */
	err = jd79661_cmd(dev, JD79661_CMD_PWR_ON, NULL, 0);
	if (err < 0) {
		return err;
	}
	return jd79661_busy_wait(dev, JD79661_BUSY_TIMEOUT_POWER_ON_MS);
}

/*
 * One complete update cycle. The controller must be re-initialized after
 * every refresh and only a hardware reset wakes it from deep sleep, so
 * each cycle starts from a clean reset. Returns 0 on success.
 */
static int jd79661_update_once(const struct device *dev)
{
	const struct jd79661_config *config = dev->config;
	struct jd79661_data *data = dev->data;
	static const uint8_t refresh_cdi = 0x37;
	int err;

	/* Hardware reset (also wakes the controller from deep sleep). */
	err = mipi_dbi_reset(config->mipi_dev, JD79661_RESET_LOW_MS);
	if (err < 0) {
		LOG_ERR("reset failed: %d", err);
		return err;
	}
	k_msleep(JD79661_RESET_HIGH_MS);

	err = jd79661_busy_wait(dev, JD79661_BUSY_TIMEOUT_RESET_MS);
	if (err < 0) {
		return err;
	}

	err = jd79661_init_registers(dev);
	if (err < 0) {
		return err;
	}

	/* Write the full 2bpp RAM image. */
	err = jd79661_cmd(dev, JD79661_CMD_RAM_WRITE, data->ram,
			  JD79661_RAM_BYTES);
	if (err < 0) {
		LOG_ERR("RAM write failed: %d", err);
		return err;
	}

	/* Full refresh (CDI 0x37 = full, 0x97 would be partial). */
	err = jd79661_cmd(dev, JD79661_CMD_VCOM_DATA_INTERVAL, &refresh_cdi, 1);
	if (err < 0) {
		return err;
	}
	err = jd79661_cmd(dev, JD79661_CMD_DISPLAY_REFRESH, NULL, 0);
	if (err < 0) {
		return err;
	}
	err = jd79661_busy_wait(dev, JD79661_BUSY_TIMEOUT_REFRESH_MS);
	if (err < 0) {
		return err;
	}

	/* Power off, then deep sleep: the image stays on the panel and no
	 * DC bias is applied between updates.
	 */
	err = jd79661_cmd(dev, JD79661_CMD_PWR_OFF, NULL, 0);
	if (err < 0) {
		return err;
	}
	err = jd79661_busy_wait(dev, JD79661_BUSY_TIMEOUT_POWER_OFF_MS);
	if (err < 0) {
		return err;
	}

	static const uint8_t deep_sleep_code = 0xA5;

	return jd79661_cmd(dev, JD79661_CMD_DEEP_SLEEP, &deep_sleep_code, 1);
}

/* Full update with one retry: a transient bus failure cannot swallow
 * a screen update.
 */
static int jd79661_update(const struct device *dev)
{
	int err = jd79661_update_once(dev);

	if (err != 0) {
		LOG_WRN("update failed (%d), retrying once", err);
		k_msleep(50);
		err = jd79661_update_once(dev);
	}
	return err;
}

static int jd79661_blanking_on(const struct device *dev)
{
	struct jd79661_data *data = dev->data;

	/* Only fill the RAM shadow; the refresh happens on unblank. */
	data->blanking = true;
	return 0;
}

static int jd79661_blanking_off(const struct device *dev)
{
	struct jd79661_data *data = dev->data;

	if (!data->blanking) {
		return 0;
	}
	data->blanking = false;

	return jd79661_update(dev);
}

/*
 * Convert a row-major 1bpp buffer (MONO01: bit set = white, MSB first,
 * pitch = width, rows of width/8 bytes) into the 2bpp RAM shadow.
 * Screen x maps to gate line (x/32 in the shadow, one gate line per
 * 32-byte row), screen y to source (y/4 in the byte, bits 6-2*(y%4)).
 * Pixels beyond the visible area are clipped (x >= 250 or y >= 122).
 */
static int jd79661_write(const struct device *dev, uint16_t x, uint16_t y,
			 const struct display_buffer_descriptor *desc,
			 const void *buf)
{
	const struct jd79661_config *config = dev->config;
	struct jd79661_data *data = dev->data;
	const uint8_t *src = buf;

	if (x >= config->width || y >= config->height) {
		return -EINVAL;
	}
	if (desc->pitch != desc->width || desc->width % 8 != 0 ||
	    desc->buf_size != (uint32_t)desc->width * desc->height / 8) {
		return -EINVAL;
	}

	size_t row_bytes = desc->width / 8;

	for (uint16_t r = 0; r < desc->height; r++) {
		uint16_t sy = y + r;

		if (sy >= config->height) {
			break;
		}
		uint8_t shift = 6 - 2 * (sy % 4);
		for (uint16_t c = 0; c < row_bytes; c++) {
			uint8_t v = src[(uint32_t)r * row_bytes + c];
			uint16_t sx = x + 8 * c;

			for (uint8_t b = 0; b < 8; b++, sx++) {
				if (sx >= config->width) {
					break;
				}
				uint8_t code = (v >> (7 - b)) & 1U
						       ? JD79661_COLOR_WHITE
						       : JD79661_COLOR_BLACK;
				uint32_t idx = (uint32_t)sx * 32 + sy / 4;

				data->ram[idx] = (data->ram[idx] & ~(3 << shift)) |
						 (code << shift);
			}
		}
	}

	return 0;
}

static int jd79661_get_capabilities(const struct device *dev,
				    struct display_capabilities *caps)
{
	const struct jd79661_config *config = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->current_pixel_format = PIXEL_FORMAT_MONO01;
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO01;
	/* EPD: the LVGL port brackets multi-chunk renders with blanking. */
	caps->screen_info = SCREEN_INFO_EPD | SCREEN_INFO_MONO_MSB_FIRST;
	return 0;
}

int jd79661_display_frame(const struct device *dev, const uint8_t *frame)
{
	if (dev == NULL || dev->api != &jd79661_driver_api) {
		return -ENODEV;
	}

	struct jd79661_data *data = dev->data;

	memcpy(data->ram, frame, JD79661_RAM_BYTES);
	data->blanking = false;

	return jd79661_update(dev);
}

static int jd79661_init(const struct device *dev)
{
	const struct jd79661_config *config = dev->config;
	struct jd79661_data *data = dev->data;

	if (!device_is_ready(config->mipi_dev)) {
		LOG_ERR("MIPI DBI device not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&config->busy_gpio)) {
		LOG_ERR("BUSY gpio not ready");
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);

	if (err < 0) {
		LOG_ERR("BUSY gpio configure failed: %d", err);
		return err;
	}

	/* No panel commands at boot: the panel stays untouched (deep sleep
	 * from the previous update) until the first refresh cycle.
	 */
	data->blanking = true;
	memset(data->ram, 0, sizeof(data->ram));
	return 0;
}

#define JD79661_DEFINE(n)							\
	static const struct jd79661_config jd79661_config_##n = {		\
		.mipi_dev = DEVICE_DT_GET(DT_PARENT(n)),			\
		.dbi_config = {						\
			.mode = MIPI_DBI_MODE_SPI_4WIRE,			\
			.config = MIPI_DBI_SPI_CONFIG_DT(n,		\
				SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |	\
				SPI_HOLD_ON_CS | SPI_LOCK_ON, 0),	\
		},							\
		.busy_gpio = GPIO_DT_SPEC_GET(n, busy_gpios),		\
		.width = DT_PROP(n, width),				\
		.height = DT_PROP(n, height),				\
	};								\
									\
	static struct jd79661_data jd79661_data_##n;			\
									\
	DEVICE_DT_DEFINE(n, jd79661_init, NULL,			\
			 &jd79661_data_##n, &jd79661_config_##n,		\
			 POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,	\
			 &jd79661_driver_api);

DT_FOREACH_STATUS_OKAY(jd79661_gdey0213f51, JD79661_DEFINE)

static const struct display_driver_api jd79661_driver_api = {
	.blanking_on = jd79661_blanking_on,
	.blanking_off = jd79661_blanking_off,
	.write = jd79661_write,
	.get_capabilities = jd79661_get_capabilities,
};
