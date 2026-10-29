/*
 * Good Display GDEY0213F51 (JD79661) 2.13" quad-color ePaper driver.
 */

#ifndef JD79661_H__
#define JD79661_H__

#include <zephyr/device.h>
#include <stdint.h>

/** Panel RAM image size: 250 gate lines x 32 bytes (128 sources, 2bpp). */
#define JD79661_RAM_BYTES 8000

/** 2bpp color codes as stored in the panel RAM. */
#define JD79661_COLOR_BLACK  0x00
#define JD79661_COLOR_WHITE  0x01
#define JD79661_COLOR_YELLOW 0x02
#define JD79661_COLOR_RED    0x03

/**
 * @brief Display a complete 2bpp panel RAM image (XEPR format 2 payload).
 *
 * Copies @p frame into the driver's RAM shadow and performs a full
 * init/write/refresh/power-off cycle, retrying once on a transient
 * failure. Only valid on a device instantiated by the jd79661 driver.
 *
 * @retval 0        Image displayed.
 * @retval -ENODEV  @p dev is not a JD79661 panel device.
 * @retval -EIO     Refresh failed twice (bus or BUSY timeout).
 */
int jd79661_display_frame(const struct device *dev, const uint8_t *frame);

#endif /* JD79661_H__ */
