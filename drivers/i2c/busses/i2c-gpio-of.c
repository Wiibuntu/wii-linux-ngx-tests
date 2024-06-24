/*
 * drivers/i2c/busses/i2c-gpio-of.c
 *
 * GPIO-based bitbanging I2C driver with OF bindings
 * Copyright (C) 2009 Albert Herranz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "i2c-gpio-common.h"

#include <linux/of_platform.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c-algo-bit.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#define DRV_MODULE_NAME "i2c-gpio-of"
#define DRV_DESCRIPTION "GPIO-based bitbanging I2C driver with OF bindings"
#define DRV_AUTHOR "Albert Herranz"

#define drv_printk(level, format, arg...) \
	printk(level DRV_MODULE_NAME ": " format , ## arg)


/*
 * Taken from drivers/i2c/busses/i2c-gpio.c
 *
 */
struct i2c_gpio_private_data {
	struct gpio_desc *sda;
	struct gpio_desc *scl;
	struct i2c_adapter adap;
	struct i2c_algo_bit_data bit_data;
	struct i2c_gpio_platform_data pdata;
#ifdef CONFIG_I2C_GPIO_FAULT_INJECTOR
	struct dentry *debug_dir;
	/* these must be protected by bus lock */
	struct completion scl_irq_completion;
	u64 scl_irq_data;
#endif
};



/*
 * OF platform bindings.
 *
 */

static int i2c_gpio_of_probe(struct platform_device *odev)
{
	struct i2c_gpio_platform_data *pdata;
	struct i2c_gpio_private_data *priv;
	struct i2c_adapter *adap;
	struct gpio_desc *sda_gpiod, *scl_gpiod;
	const unsigned long *prop;
	int error = -ENOMEM;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!pdata || !priv)
		goto err_alloc_pdata;
	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (!adap)
		goto err_alloc_adap;

	sda_gpiod = devm_gpiod_get_index(&odev->dev, NULL, 0, GPIOD_ASIS);
	scl_gpiod = devm_gpiod_get_index(&odev->dev, NULL, 1, GPIOD_ASIS);
	if (IS_ERR(sda_gpiod) || IS_ERR(scl_gpiod)) {
		error = PTR_ERR(sda_gpiod);
		if (!IS_ERR(scl_gpiod))
			error = PTR_ERR(scl_gpiod);
		pr_err("%s: invalid GPIO pins\n", odev->dev.of_node->full_name);
		goto err_gpio_pin;
	}

	priv->sda = desc_to_gpio(sda_gpiod);
	priv->scl = desc_to_gpio(scl_gpiod);

	prop = of_get_property(odev->dev.of_node, "sda-is-open-drain", NULL);
	if (prop)
		pdata->sda_is_open_drain = *prop;
	prop = of_get_property(odev->dev.of_node, "sda-enforce-dir", NULL);
	if (prop)
		pdata->sda_enforce_dir = *prop;
	prop = of_get_property(odev->dev.of_node, "scl-is-open-drain", NULL);
	if (prop)
		pdata->scl_is_open_drain = *prop;
	prop = of_get_property(odev->dev.of_node, "scl-is-output-only", NULL);
	if (prop)
		pdata->scl_is_output_only = *prop;
	prop = of_get_property(odev->dev.of_node, "udelay", NULL);
	if (prop)
		pdata->udelay = *prop;
	prop = of_get_property(odev->dev.of_node, "timeout", NULL);
	if (prop)
		pdata->timeout = msecs_to_jiffies(*prop);

	error = i2c_gpio_adapter_probe(adap, pdata, &odev->dev,
		odev->dev.of_node->phandle, THIS_MODULE);

	if (error)
		goto err_probe;

	dev_set_drvdata(&odev->dev, adap);

	return 0;

err_probe:
err_gpio_pin:
	kfree(adap);
err_alloc_adap:
	kfree(pdata);
	kfree(priv);
err_alloc_pdata:
	return error;
};

static int i2c_gpio_of_remove(struct platform_device *odev)
{
	struct i2c_gpio_platform_data *pdata;
	struct i2c_adapter *adap;
	struct i2c_algo_bit_data *bit_data;

	adap = dev_get_drvdata(&odev->dev);
	bit_data = adap->algo_data;
	pdata = bit_data->data;

	i2c_gpio_adapter_remove(adap, pdata);
	kfree(pdata);
	kfree(adap);
	return 0;
};

static const struct of_device_id i2c_gpio_of_match[] = {
	{.compatible = "virtual,i2c-gpio",},
	{},
};
MODULE_DEVICE_TABLE(of, i2c_gpio_of_match);

static struct platform_driver i2c_gpio_of_driver = {
	.driver = {
		.name = DRV_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = i2c_gpio_of_match,
	},
	.probe = i2c_gpio_of_probe,
	.remove = i2c_gpio_of_remove,
};

static int __init i2c_gpio_of_init(void)
{
	int error;

	error = platform_driver_register(&i2c_gpio_of_driver);
	if (error)
		drv_printk(KERN_ERR, "OF registration failed (%d)\n", error);

	return error;
}

static void __exit i2c_gpio_of_exit(void)
{
	platform_driver_unregister(&i2c_gpio_of_driver);
}

module_init(i2c_gpio_of_init);
module_exit(i2c_gpio_of_exit);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_LICENSE("GPL");

