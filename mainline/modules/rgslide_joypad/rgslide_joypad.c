// SPDX-License-Identifier: GPL-2.0
/*
 * Anbernic RG Slide (UMS9620 / T820) integrated gamepad driver for Linux 6.18.
 *
 * Binds both:
 *  - "sprd,spi-mcu-joy" (spi0.0): powers the analog stick/trigger MCU and
 *    reads 6x 16-bit analog axes via a 64-byte full-duplex SPI packet
 *    (TX: 0x5a 0x3a 0x66 ...; RX[32..47]: 0x5a 0x3a + 6x LE16 + LE16 sum).
 *  - "singleadc-joypad": polls all 18 GPIO switches (sw1..sw18) and the 6
 *    analog axes, registering the standard "retrogame_joypad" evdev device
 *    (BUS_HOST 0x19, vid 0x484b, pid 0x1101, ver 0x0100).
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#define RGSLIDE_MCU_BUF_LEN	64
#define RGSLIDE_MAX_BTNS	32

struct rgslide_mcu {
	struct spi_device *spi;
	struct regulator *sim_vdd;
	struct gpio_desc *pwr_gpio;
	struct gpio_desc *rst_gpio;
	struct gpio_desc *wake_gpio;
	struct mutex lock;
	u8 tx_buf[RGSLIDE_MCU_BUF_LEN];
	u8 rx_buf[RGSLIDE_MCU_BUF_LEN];
};

static struct rgslide_mcu *g_mcu;
static DEFINE_MUTEX(g_mcu_lock);

static int rgslide_mcu_read_axes(u16 out[6])
{
	struct spi_transfer xfer = { };
	struct spi_message msg;
	u16 sum = 0, rx_sum;
	int ret, i;

	mutex_lock(&g_mcu_lock);
	if (!g_mcu) {
		mutex_unlock(&g_mcu_lock);
		return -ENODEV;
	}

	mutex_lock(&g_mcu->lock);
	memset(g_mcu->rx_buf, 0, sizeof(g_mcu->rx_buf));
	xfer.tx_buf = g_mcu->tx_buf;
	xfer.rx_buf = g_mcu->rx_buf;
	xfer.len = RGSLIDE_MCU_BUF_LEN;
	xfer.bits_per_word = 8;
	xfer.word_delay.unit = SPI_DELAY_UNIT_SCK;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(g_mcu->spi, &msg);
	if (ret)
		goto out_unlock;

	if (g_mcu->rx_buf[32] != 0x5a || g_mcu->rx_buf[33] != 0x3a) {
		ret = -EPROTO;
		goto out_unlock;
	}

	for (i = 34; i <= 45; i++)
		sum += g_mcu->rx_buf[i];

	rx_sum = g_mcu->rx_buf[46] | ((u16)g_mcu->rx_buf[47] << 8);
	if (sum != rx_sum) {
		ret = -EBADMSG;
		goto out_unlock;
	}

	/* Order matching stock joypad_spi_data_parse (each raw 12-bit << 3) */
	out[0] = (g_mcu->rx_buf[36] | ((u16)g_mcu->rx_buf[37] << 8)) << 3; /* ABS_X  */
	out[1] = (g_mcu->rx_buf[34] | ((u16)g_mcu->rx_buf[35] << 8)) << 3; /* ABS_Y  */
	out[2] = (g_mcu->rx_buf[40] | ((u16)g_mcu->rx_buf[41] << 8)) << 3; /* ABS_RX */
	out[3] = (g_mcu->rx_buf[38] | ((u16)g_mcu->rx_buf[39] << 8)) << 3; /* ABS_RY */
	out[4] = (g_mcu->rx_buf[42] | ((u16)g_mcu->rx_buf[43] << 8)) << 3; /* ABS_Z  */
	out[5] = (g_mcu->rx_buf[44] | ((u16)g_mcu->rx_buf[45] << 8)) << 3; /* ABS_RZ */
	ret = 0;

out_unlock:
	mutex_unlock(&g_mcu->lock);
	mutex_unlock(&g_mcu_lock);
	return ret;
}

static int rgslide_mcu_probe(struct spi_device *spi)
{
	struct rgslide_mcu *mcu;
	int ret;

	mcu = devm_kzalloc(&spi->dev, sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = 1200000;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	mcu->spi = spi;
	mutex_init(&mcu->lock);
	mcu->tx_buf[0] = 0x5a;
	mcu->tx_buf[1] = 0x3a;
	mcu->tx_buf[2] = 0x66;

	mcu->sim_vdd = devm_regulator_get_optional(&spi->dev, "sim-vdd");
	if (!IS_ERR(mcu->sim_vdd)) {
		ret = regulator_enable(mcu->sim_vdd);
		if (ret)
			dev_warn(&spi->dev, "sim-vdd enable failed: %d\n", ret);
	}

	mcu->pwr_gpio = devm_gpiod_get_optional(&spi->dev, "mcu-pwr", GPIOD_OUT_HIGH);
	if (!IS_ERR_OR_NULL(mcu->pwr_gpio))
		gpiod_set_value_cansleep(mcu->pwr_gpio, 1);

	mcu->rst_gpio = devm_gpiod_get_optional(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (!IS_ERR_OR_NULL(mcu->rst_gpio)) {
		gpiod_set_value_cansleep(mcu->rst_gpio, 1);
		msleep(10);
		gpiod_set_value_cansleep(mcu->rst_gpio, 0);
	}

	mcu->wake_gpio = devm_gpiod_get_optional(&spi->dev, "wakeup", GPIOD_OUT_HIGH);

	spi_set_drvdata(spi, mcu);
	mutex_lock(&g_mcu_lock);
	g_mcu = mcu;
	mutex_unlock(&g_mcu_lock);

	dev_info(&spi->dev, "rgslide SPI joypad MCU ready (%u Hz)\n", spi->max_speed_hz);
	return 0;
}

static void rgslide_mcu_remove(struct spi_device *spi)
{
	struct rgslide_mcu *mcu = spi_get_drvdata(spi);

	mutex_lock(&g_mcu_lock);
	if (g_mcu == mcu)
		g_mcu = NULL;
	mutex_unlock(&g_mcu_lock);

	if (!IS_ERR_OR_NULL(mcu->sim_vdd))
		regulator_disable(mcu->sim_vdd);
}

static const struct of_device_id rgslide_mcu_of_match[] = {
	{ .compatible = "sprd,spi-mcu-joy" },
	{ }
};
MODULE_DEVICE_TABLE(of, rgslide_mcu_of_match);

static struct spi_driver rgslide_mcu_driver = {
	.driver = {
		.name = "rgslide-spi-mcu-joy",
		.of_match_table = rgslide_mcu_of_match,
	},
	.probe = rgslide_mcu_probe,
	.remove = rgslide_mcu_remove,
};

struct rgslide_btn {
	struct gpio_desc *gpiod;
	u32 type;
	u32 code;
	u32 abs_val;
};

struct rgslide_joypad {
	struct device *dev;
	struct input_dev *input;
	struct regulator *vdd;
	struct rgslide_btn btns[RGSLIDE_MAX_BTNS];
	int num_btns;
	u16 center[4];
	u16 trig_rest[2];
	bool calibrated;
};

static const u32 axis_codes[6] = {
	ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ
};

static void rgslide_joypad_poll(struct input_dev *input)
{
	struct rgslide_joypad *jp = input_get_drvdata(input);
	int hat_x = 0, hat_y = 0;
	u16 axes[6];
	int i;

	for (i = 0; i < jp->num_btns; i++) {
		struct rgslide_btn *b = &jp->btns[i];
		int pressed = gpiod_get_value_cansleep(b->gpiod);

		if (pressed < 0)
			continue;

		if (b->type == EV_ABS) {
			if (pressed) {
				int dir = (b->abs_val == 2) ? -1 : 1;

				if (b->code == ABS_HAT0X)
					hat_x = dir;
				else if (b->code == ABS_HAT0Y)
					hat_y = dir;
			}
		} else {
			input_report_key(input, b->code, pressed);
		}
	}

	input_report_abs(input, ABS_HAT0X, hat_x);
	input_report_abs(input, ABS_HAT0Y, hat_y);

	if (rgslide_mcu_read_axes(axes) == 0) {
		if (!jp->calibrated) {
			for (i = 0; i < 4; i++)
				jp->center[i] = axes[i] ? axes[i] : 16384;
			jp->trig_rest[0] = axes[4];
			jp->trig_rest[1] = axes[5];
			jp->calibrated = true;
		}
		for (i = 0; i < 4; i++) {
			int val = (int)axes[i] - (int)jp->center[i];

			val = clamp(val, -16384, 16383);
			input_report_abs(input, axis_codes[i], val);
		}
		input_report_abs(input, ABS_Z,
				 clamp(abs((int)axes[4] - (int)jp->trig_rest[0]), 0, 32767));
		input_report_abs(input, ABS_RZ,
				 clamp(abs((int)axes[5] - (int)jp->trig_rest[1]), 0, 32767));
	}

	input_sync(input);
}

static int rgslide_joypad_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child;
	struct rgslide_joypad *jp;
	const char *name = "retrogame_joypad";
	u32 product = 0x1101, revision = 0x0100, poll_ms = 8;
	int ret, i;

	jp = devm_kzalloc(dev, sizeof(*jp), GFP_KERNEL);
	if (!jp)
		return -ENOMEM;

	jp->dev = dev;
	of_property_read_string(dev->of_node, "joypad-name", &name);
	of_property_read_u32(dev->of_node, "joypad-product", &product);
	of_property_read_u32(dev->of_node, "joypad-revision", &revision);
	of_property_read_u32(dev->of_node, "poll-interval", &poll_ms);
	if (poll_ms < 4)
		poll_ms = 4;

	jp->vdd = devm_regulator_get_optional(dev, "vdd");
	if (!IS_ERR(jp->vdd)) {
		ret = regulator_enable(jp->vdd);
		if (ret)
			dev_warn(dev, "vdd enable failed: %d\n", ret);
	}

	jp->input = devm_input_allocate_device(dev);
	if (!jp->input)
		return -ENOMEM;

	jp->input->name = name;
	jp->input->phys = "rgslide-joypad/input0";
	jp->input->id.bustype = BUS_HOST;
	jp->input->id.vendor = 0x484b;
	jp->input->id.product = product;
	jp->input->id.version = revision;
	input_set_drvdata(jp->input, jp);

	for_each_child_of_node(dev->of_node, child) {
		struct rgslide_btn *b;
		struct gpio_desc *gpiod;
		u32 code = 0, type = EV_KEY, abs_val = 0;

		if (jp->num_btns >= RGSLIDE_MAX_BTNS) {
			of_node_put(child);
			break;
		}
		if (of_property_read_u32(child, "linux,code", &code))
			continue;
		of_property_read_u32(child, "linux,input-type", &type);
		of_property_read_u32(child, "linux,abs-value", &abs_val);

		gpiod = devm_fwnode_gpiod_get(dev, of_fwnode_handle(child),
					      NULL, GPIOD_IN, child->name);
		if (IS_ERR(gpiod)) {
			dev_warn(dev, "skip %pOFn: gpiod error %ld\n",
				 child, PTR_ERR(gpiod));
			continue;
		}

		b = &jp->btns[jp->num_btns++];
		b->gpiod = gpiod;
		b->type = type;
		b->code = code;
		b->abs_val = abs_val;

		if (type == EV_ABS)
			input_set_abs_params(jp->input, code, -1, 1, 0, 0);
		else
			input_set_capability(jp->input, EV_KEY, code);
	}

	for (i = 0; i < 4; i++)
		input_set_abs_params(jp->input, axis_codes[i], -16384, 16383, 64, 512);
	input_set_abs_params(jp->input, ABS_Z, 0, 32767, 64, 64);
	input_set_abs_params(jp->input, ABS_RZ, 0, 32767, 64, 64);

	ret = input_setup_polling(jp->input, rgslide_joypad_poll);
	if (ret)
		return ret;
	input_set_poll_interval(jp->input, poll_ms);

	ret = input_register_device(jp->input);
	if (ret)
		return ret;

	dev_info(dev, "registered %s (%d GPIO buttons + SPI MCU sticks)\n",
		 name, jp->num_btns);
	return 0;
}

static const struct of_device_id rgslide_joypad_of_match[] = {
	{ .compatible = "singleadc-joypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, rgslide_joypad_of_match);

static struct platform_driver rgslide_joypad_driver = {
	.driver = {
		.name = "rgslide-joypad",
		.of_match_table = rgslide_joypad_of_match,
	},
	.probe = rgslide_joypad_probe,
};

static int __init rgslide_joypad_init(void)
{
	int ret;

	ret = spi_register_driver(&rgslide_mcu_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&rgslide_joypad_driver);
	if (ret) {
		spi_unregister_driver(&rgslide_mcu_driver);
		return ret;
	}
	return 0;
}

static void __exit rgslide_joypad_exit(void)
{
	platform_driver_unregister(&rgslide_joypad_driver);
	spi_unregister_driver(&rgslide_mcu_driver);
}

module_init(rgslide_joypad_init);
module_exit(rgslide_joypad_exit);

MODULE_DESCRIPTION("Anbernic RG Slide Integrated Gamepad Driver");
MODULE_LICENSE("GPL v2");
