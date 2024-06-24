/*
 * Core logic for the bitbanging I2C bus driver using the GPIO API
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "i2c-gpio-common.h"

#include <linux/i2c-algo-bit.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>

struct i2c_gpio {
    struct gpio_desc *sda_gpiod;
    struct gpio_desc *scl_gpiod;
};

/* Toggle SDA by changing the direction of the pin */
static void i2c_gpio_setsda_dir(void *data, int state)
{
    struct i2c_gpio *gpios = data;

    if (state)
        gpiod_direction_input(gpios->sda_gpiod);
    else
        gpiod_direction_output(gpios->sda_gpiod, 0);
}

/*
 * Toggle SDA by changing the output value of the pin. This is only
 * valid for pins configured as open drain (i.e. setting the value
 * high effectively turns off the output driver.)
 */
static void i2c_gpio_setsda_val(void *data, int state)
{
    struct i2c_gpio *gpios = data;

    gpiod_set_value(gpios->sda_gpiod, state);
}

/*
 * Toggle SDA by changing the output value of the pin, first making sure
 * that the pin is configured as an output.
 */
static void i2c_gpio_setsda_val_dir(void *data, int state)
{
    struct i2c_gpio *gpios = data;

    if (!gpiod_get_direction(gpios->sda_gpiod))
        gpiod_direction_output(gpios->sda_gpiod, state);
    else
        gpiod_set_value(gpios->sda_gpiod, state);
}

/* Toggle SCL by changing the direction of the pin. */
static void i2c_gpio_setscl_dir(void *data, int state)
{
    struct i2c_gpio *gpios = data;

    if (state)
        gpiod_direction_input(gpios->scl_gpiod);
    else
        gpiod_direction_output(gpios->scl_gpiod, 0);
}

/*
 * Toggle SCL by changing the output value of the pin. This is used
 * for pins that are configured as open drain and for output-only
 * pins. The latter case will break the i2c protocol, but it will
 * often work in practice.
 */
static void i2c_gpio_setscl_val(void *data, int state)
{
    struct i2c_gpio *gpios = data;

    gpiod_set_value(gpios->scl_gpiod, state);
}

static int i2c_gpio_getsda(void *data)
{
    struct i2c_gpio *gpios = data;

    return gpiod_get_value(gpios->sda_gpiod);
}

/*
 * Read SDA value from the pin, first making sure that the pin is
 * configured as an input.
 */
static int i2c_gpio_getsda_val_dir(void *data)
{
    struct i2c_gpio *gpios = data;

    if (gpiod_get_direction(gpios->sda_gpiod))
        gpiod_direction_input(gpios->sda_gpiod);
    return gpiod_get_value(gpios->sda_gpiod);
}

static int i2c_gpio_getscl(void *data)
{
    struct i2c_gpio *gpios = data;

    return gpiod_get_value(gpios->scl_gpiod);
}

int i2c_gpio_adapter_probe(struct i2c_adapter *adap,
                           struct i2c_gpio_platform_data *pdata,
                           struct device *parent, int id,
                           struct module *owner)
{
    struct i2c_algo_bit_data *bit_data;
    struct i2c_gpio *gpios;
    int error;

    error = -ENOMEM;
    bit_data = kzalloc(sizeof(*bit_data), GFP_KERNEL);
    if (!bit_data)
        goto err_alloc_bit_data;

    gpios = kzalloc(sizeof(*gpios), GFP_KERNEL);
    if (!gpios)
        goto err_alloc_gpios;

    gpios->sda_gpiod = devm_gpiod_get_index(parent, "sda", 0, GPIOD_ASIS);
    if (IS_ERR(gpios->sda_gpiod)) {
        error = PTR_ERR(gpios->sda_gpiod);
        goto err_get_sda;
    }

    gpios->scl_gpiod = devm_gpiod_get_index(parent, "scl", 0, GPIOD_ASIS);
    if (IS_ERR(gpios->scl_gpiod)) {
        error = PTR_ERR(gpios->scl_gpiod);
        goto err_get_scl;
    }

    if (pdata->sda_is_open_drain) {
        gpiod_direction_output(gpios->sda_gpiod, 1);
        if (pdata->sda_enforce_dir)
            bit_data->setsda = i2c_gpio_setsda_val_dir;
        else
            bit_data->setsda = i2c_gpio_setsda_val;
    } else {
        gpiod_direction_input(gpios->sda_gpiod);
        bit_data->setsda = i2c_gpio_setsda_dir;
    }

    if (pdata->scl_is_open_drain || pdata->scl_is_output_only) {
        gpiod_direction_output(gpios->scl_gpiod, 1);
        bit_data->setscl = i2c_gpio_setscl_val;
    } else {
        gpiod_direction_input(gpios->scl_gpiod);
        bit_data->setscl = i2c_gpio_setscl_dir;
    }

    if (!pdata->scl_is_output_only)
        bit_data->getscl = i2c_gpio_getscl;
    if (pdata->sda_enforce_dir)
        bit_data->getsda = i2c_gpio_getsda_val_dir;
    else
        bit_data->getsda = i2c_gpio_getsda;

    if (pdata->udelay)
        bit_data->udelay = pdata->udelay;
    else if (pdata->scl_is_output_only)
        bit_data->udelay = 50;            /* 10 kHz */
    else
        bit_data->udelay = 5;            /* 100 kHz */

    if (pdata->timeout)
        bit_data->timeout = pdata->timeout;
    else
        bit_data->timeout = HZ / 10;        /* 100 ms */

    bit_data->data = gpios;

    adap->owner = owner;
    snprintf(adap->name, sizeof(adap->name), "i2c-gpio%d", id);
    adap->algo_data = bit_data;
    adap->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
    adap->dev.parent = parent;

    /*
     * If "id" is negative we consider it as zero.
     * The reason to do so is to avoid sysfs names that only make
     * sense when there are multiple adapters.
     */
    adap->nr = (id != -1) ? id : 0;
    error = i2c_bit_add_numbered_bus(adap);
    if (error)
        goto err_add_bus;

    dev_info(parent, "using pins %u (SDA) and %u (SCL%s)\n",
             desc_to_gpio(gpios->sda_gpiod), desc_to_gpio(gpios->scl_gpiod),
             pdata->scl_is_output_only
             ? ", no clock stretching" : "");

    return 0;

err_add_bus:
    devm_gpiod_put(parent, gpios->scl_gpiod);
err_get_scl:
    devm_gpiod_put(parent, gpios->sda_gpiod);
err_get_sda:
    kfree(gpios);
err_alloc_gpios:
    kfree(bit_data);
err_alloc_bit_data:
    return error;
}
EXPORT_SYMBOL(i2c_gpio_adapter_probe);

int i2c_gpio_adapter_remove(struct i2c_adapter *adap,
                            struct i2c_gpio_platform_data *pdata)
{
    struct i2c_gpio *gpios = adap->algo_data;

    i2c_del_adapter(adap);
    devm_gpiod_put(adap->dev.parent, gpios->scl_gpiod);
    devm_gpiod_put(adap->dev.parent, gpios->sda_gpiod);
    kfree(gpios);
    kfree(adap->algo_data);

    return 0;
}
EXPORT_SYMBOL(i2c_gpio_adapter_remove);

MODULE_AUTHOR("Haavard Skinnemoen <hskinnemoen@atmel.com>");
MODULE_DESCRIPTION("Platform-independent bitbanging I2C driver common logic");
MODULE_LICENSE("GPL");

