/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Driver for Ilitek ili8960 LCD
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/delay.h>

struct ili8960 {
	struct spi_device *spi;
	struct lcd_device *lcd;
	struct backlight_device *bl;
	unsigned enabled:1;
};

static int ili8960_write_reg(struct spi_device *spi, uint8_t reg,
				uint8_t data)
{
	uint8_t buf[2];
	buf[0] = ((reg & 0x40) << 1) | (reg & 0x3f);
	buf[1] = data;

	return spi_write(spi, buf, sizeof(buf));
}

static void ili8960_power_disable(struct spi_device *spi)
{
	int ret = ili8960_write_reg(spi, 0x5, 0xc6) ;
	if (ret < 0)
		dev_err(&spi->dev, "Failed to disable power: %d\n", ret);
}

static void ili8960_power_enable(struct spi_device *spi)
{
	ili8960_write_reg(spi, 0x5, 0xc7);
}


static int ili8960_set_power(struct lcd_device *lcd, int power)
{
	struct ili8960 *ili8960 = lcd_get_data(lcd);

	switch (power) {
	case FB_BLANK_UNBLANK:
		mdelay(20);
		ili8960->enabled = 1;
		ili8960_power_enable(ili8960->spi);
		break;
	default:
		ili8960->enabled = 0;
		ili8960_power_disable(ili8960->spi);
		mdelay(20);
		break;
	}
	return 0;
}

static int ili8960_set_contrast(struct lcd_device *lcd, int contrast)
{
	struct ili8960 *ili8960 = lcd_get_data(lcd);
	ili8960_write_reg(ili8960->spi, 0x0d, contrast);
	return 0;
}

static int ili8960_set_mode(struct lcd_device *lcd, struct fb_videomode *mode)
{
	if (mode->xres != 320 && mode->yres != 240)
		return -EINVAL;

	return 0;
}

static ssize_t reg_write(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	char *buf2;
	uint32_t reg = simple_strtoul(buf, &buf2, 10);
	uint32_t val = simple_strtoul(buf2 + 1, NULL, 10);
	struct ili8960 *ili8960 = dev_get_drvdata(dev);

	if (reg < 0 || val < 0)
		return -EINVAL;

	ili8960_write_reg(ili8960->spi, reg, val);
	return count;
}

static DEVICE_ATTR(reg, 0644, NULL, reg_write);

static ssize_t ili8960_show_brightness(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int rc = -ENXIO;

	return rc;
}

static ssize_t ili8960_store_brightness(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	char *endp;
	struct lcd_device *ld = to_lcd_device(dev);
	struct ili8960 *ili8960 = lcd_get_data(ld);
	int brightness = simple_strtoul(buf, &endp, 0);

	if (brightness > 255 || brightness < 0)
		return -EINVAL;

	ili8960_write_reg(ili8960->spi, 0x3, brightness);

	return count;
}


static DEVICE_ATTR(brightness, 0644, ili8960_show_brightness,
	ili8960_store_brightness);

static struct lcd_ops ili8960_lcd_ops = {
	.set_power = ili8960_set_power,
	.set_contrast = ili8960_set_contrast,
	.set_mode = ili8960_set_mode,
};

static int __devinit ili8960_probe(struct spi_device *spi)
{
	int ret;
	struct ili8960 *ili8960;

	ili8960 = kmalloc(sizeof(*ili8960), GFP_KERNEL);

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3 | SPI_3WIRE;

	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "Failed to setup spi\n");
		goto err_free_ili8960;
	}

	ili8960->spi = spi;

	ili8960->lcd = lcd_device_register("ili8960-lcd", &spi->dev, ili8960,
						&ili8960_lcd_ops);

	if (IS_ERR(ili8960->lcd)) {
		ret = PTR_ERR(ili8960->lcd);
		dev_err(&spi->dev, "Failed to register lcd device: %d\n", ret);
		goto err_free_ili8960;
	}

	ili8960->lcd->props.max_contrast = 255;

	ret = device_create_file(&spi->dev, &dev_attr_reg);
	if (ret)
		goto err_unregister_lcd;

	ret = device_create_file(&ili8960->lcd->dev, &dev_attr_brightness);
	if (ret)
		goto err_unregister_lcd;

	ili8960->enabled = 1;
	dev_set_drvdata(&spi->dev, ili8960);

	ili8960_write_reg(spi, 0x13, 0x01);
	ili8960_write_reg(spi, 0x5, 0xc7);

	return 0;
err_unregister_lcd:
	lcd_device_unregister(ili8960->lcd);
err_free_ili8960:
	kfree(ili8960);
	return ret;
}

static int __devexit ili8960_remove(struct spi_device *spi)
{
	struct ili8960 *ili8960 = spi_get_drvdata(spi);
#if 0
	if (ili8960->bl)
		backlight_device_unregister(ili8960->bl);
#endif

	lcd_device_unregister(ili8960->lcd);

	spi_set_drvdata(spi, NULL);
	kfree(ili8960);
	return 0;
}

#ifdef CONFIG_PM

static int ili8960_suspend(struct spi_device *spi, pm_message_t state)
{
	struct ili8960 *ili8960 = spi_get_drvdata(spi);
	if (ili8960->enabled) {
		ili8960_power_disable(spi);
		mdelay(10);
	}
	return 0;
}

static int ili8960_resume(struct spi_device *spi)
{
	struct ili8960 *ili8960 = spi_get_drvdata(spi);
	if (ili8960->enabled)
		ili8960_power_enable(spi);
	return 0;
}

#else
#define ili8960_suspend NULL
#define ili8960_resume NULL
#endif

static struct spi_driver ili8960_driver = {
	.driver = {
		.name = "ili8960",
		.owner = THIS_MODULE,
	},
	.probe = ili8960_probe,
	.remove = __devexit_p(ili8960_remove),
	.suspend = ili8960_suspend,
	.resume = ili8960_resume,
};

static int __init ili8960_init(void)
{
	return spi_register_driver(&ili8960_driver);
}
module_init(ili8960_init);

static void __exit ili8960_exit(void)
{
	return spi_unregister_driver(&ili8960_driver);
}
module_exit(ili8960_exit)

MODULE_AUTHOR("Lars-Peter Clausen");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("LCD driver for Ilitek ili8960");
MODULE_ALIAS("spi:ili8960");
