// SPDX-License-Identifier: GPL-2.0-only
/* HXM035002/DX757 CST836 single-pointer input driver. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define CST836_ADDRESS 0x15
#define CST836_FACTORY_ID 0x13
#define CST836_REPORT_SIZE 13U
#define CST836_DEFAULT_WIDTH 212U
#define CST836_DEFAULT_HEIGHT 318U
#define CST836_MAX_COORDINATE 4095U
#define CST836_POLL_MS 20U
#define CST836_MAX_ERRORS 5U

struct cst836_data {
    struct i2c_client *client;
    struct input_dev *input;
    struct gpio_desc *reset;
    struct gpio_desc *interrupt;
    struct regulator *vcc;
    struct touchscreen_properties properties;
    struct mutex lock;
    struct delayed_work poll_work;
    unsigned int width;
    unsigned int height;
    unsigned int poll_ms;
    unsigned int failures;
    int irq;
    bool running;
    bool down;
    u64 reports;
    u64 contacts;
    u64 errors;
    u64 irq_count;
};

static int cst836_read(struct i2c_client *client, u8 reg, u8 *data, u16 length)
{
    struct i2c_msg messages[] = {
        {
            .addr = client->addr,
            .flags = 0,
            .len = 1,
            .buf = &reg,
        },
        {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = length,
            .buf = data,
        },
    };
    int result;

    if (!data || length == 0 || length > CST836_REPORT_SIZE) {
        return -EINVAL;
    }
    result = i2c_transfer(client->adapter, messages, ARRAY_SIZE(messages));
    if (result != ARRAY_SIZE(messages)) {
        return result < 0 ? result : -EIO;
    }
    return 0;
}

static void cst836_release(struct cst836_data *data)
{
    if (!data->down) {
        return;
    }
    input_report_key(data->input, BTN_TOUCH, 0);
    input_sync(data->input);
    data->down = false;
}

/* IRQ and polling paths serialize I2C transfers and input state with lock. */
static void cst836_sample(struct cst836_data *data)
{
    u8 report[CST836_REPORT_SIZE];
    unsigned int count;
    unsigned int event;
    unsigned int x;
    unsigned int y;
    int result;

    if (!data->running) {
        return;
    }
    result = cst836_read(data->client, 0x00, report, sizeof(report));
    if (result != 0) {
        goto read_error;
    }
    ++data->reports;
    count = report[2] & 0x03;
    event = report[3] >> 6;
    if (count > 2 || (count > 0 && event == 3)) {
        result = -EPROTO;
        goto read_error;
    }
    if (count == 0 || event == 1) {
        data->failures = 0;
        cst836_release(data);
        goto schedule_poll;
    }
    x = ((unsigned int)(report[3] & 0x0f) << 8) | report[4];
    y = ((unsigned int)(report[5] & 0x0f) << 8) | report[6];
    if (x > data->properties.max_x || y > data->properties.max_y) {
        result = -ERANGE;
        goto read_error;
    }
    data->failures = 0;
    ++data->contacts;
    input_report_key(data->input, BTN_TOUCH, 1);
    touchscreen_report_pos(data->input, &data->properties, x, y, false);
    input_sync(data->input);
    data->down = true;
    goto schedule_poll;

read_error:
    ++data->errors;
    if (data->failures < CST836_MAX_ERRORS) {
        ++data->failures;
    }
    dev_warn_ratelimited(&data->client->dev, "touch report rejected: %d\n", result);
    if (data->failures >= CST836_MAX_ERRORS) {
        cst836_release(data);
    }
schedule_poll:
    if (data->running) {
        mod_delayed_work(system_wq, &data->poll_work, msecs_to_jiffies(data->poll_ms));
    }
}

static void cst836_poll(struct work_struct *work)
{
    struct cst836_data *data = container_of(to_delayed_work(work), struct cst836_data, poll_work);

    mutex_lock(&data->lock);
    cst836_sample(data);
    mutex_unlock(&data->lock);
}

static irqreturn_t cst836_interrupt(int irq, void *context)
{
    struct cst836_data *data = context;

    (void)irq;
    mutex_lock(&data->lock);
    ++data->irq_count;
    cst836_sample(data);
    mutex_unlock(&data->lock);
    return IRQ_HANDLED;
}

static void cst836_stop(void *context)
{
    struct cst836_data *data = context;
    bool was_running;

    mutex_lock(&data->lock);
    was_running = data->running;
    data->running = false;
    mutex_unlock(&data->lock);
    if (was_running && data->irq > 0) {
        disable_irq(data->irq);
    }
    cancel_delayed_work_sync(&data->poll_work);
    mutex_lock(&data->lock);
    cst836_release(data);
    mutex_unlock(&data->lock);
}

static void cst836_disable_supply(void *context)
{
    struct cst836_data *data = context;
    int result = regulator_disable(data->vcc);

    if (result != 0) {
        dev_warn(&data->client->dev, "failed to disable vcc: %d\n", result);
    }
}

static int cst836_power_on(struct cst836_data *data)
{
    struct device *dev = &data->client->dev;
    int result;

    data->vcc = devm_regulator_get_optional(dev, "vcc");
    if (IS_ERR(data->vcc)) {
        result = PTR_ERR(data->vcc);
        if (result != -ENODEV) {
            return result;
        }
        data->vcc = NULL;
    } else {
        result = regulator_enable(data->vcc);
        if (result != 0) {
            return result;
        }
        result = devm_add_action_or_reset(dev, cst836_disable_supply, data);
        if (result != 0) {
            return result;
        }
    }
    data->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(data->reset)) {
        return PTR_ERR(data->reset);
    }
    if (data->reset) {
        /* DT polarity defines assertion; match the previously tested pulse. */
        msleep(20);
        gpiod_set_value_cansleep(data->reset, 0);
        msleep(300);
    }
    return 0;
}

static int cst836_read_options(struct cst836_data *data)
{
    struct device *dev = &data->client->dev;
    static const char * const names[] = {"touchscreen-size-x", "touchscreen-size-y", "poll-interval"};
    u32 values[] = {CST836_DEFAULT_WIDTH, CST836_DEFAULT_HEIGHT, CST836_POLL_MS};
    unsigned int index;
    int result;

    for (index = 0; index < ARRAY_SIZE(names); ++index) {
        if (!device_property_present(dev, names[index])) {
            continue;
        }
        result = device_property_read_u32(dev, names[index], &values[index]);
        if (result != 0) {
            return result;
        }
    }
    if (values[0] < 2 || values[0] > CST836_MAX_COORDINATE + 1 ||
        values[1] < 2 || values[1] > CST836_MAX_COORDINATE + 1 || values[2] < 5 || values[2] > 1000) {
        return -EINVAL;
    }
    data->width = values[0];
    data->height = values[1];
    data->poll_ms = values[2];
    return 0;
}

static int cst836_register_input(struct cst836_data *data)
{
    struct device *dev = &data->client->dev;
    struct input_dev *input = devm_input_allocate_device(dev);

    if (!input) {
        return -ENOMEM;
    }
    data->input = input;
    input->name = "HXM035002 CST836";
    input->id.bustype = BUS_I2C;
    input->dev.parent = dev;
    __set_bit(INPUT_PROP_DIRECT, input->propbit);
    input_set_capability(input, EV_KEY, BTN_TOUCH);
    input_set_abs_params(input, ABS_X, 0, data->width - 1, 0, 0);
    input_set_abs_params(input, ABS_Y, 0, data->height - 1, 0, 0);
    touchscreen_parse_properties(input, false, &data->properties);
    if (!input->absinfo || data->properties.max_x > CST836_MAX_COORDINATE ||
        data->properties.max_y > CST836_MAX_COORDINATE ||
        input_abs_get_min(input, ABS_X) < 0 || input_abs_get_min(input, ABS_Y) < 0 ||
        input_abs_get_min(input, ABS_X) >= input_abs_get_max(input, ABS_X) ||
        input_abs_get_min(input, ABS_Y) >= input_abs_get_max(input, ABS_Y)) {
        return -EINVAL;
    }
    return input_register_device(input);
}

static int cst836_request_interrupt(struct cst836_data *data)
{
    struct device *dev = &data->client->dev;

    data->interrupt = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
    if (IS_ERR(data->interrupt)) {
        return PTR_ERR(data->interrupt);
    }
    data->irq = data->client->irq;
    if (data->interrupt) {
        int irq = gpiod_to_irq(data->interrupt);

        if (irq < 0) {
            return irq;
        }
        if (data->irq > 0 && data->irq != irq) {
            return -EINVAL;
        }
        data->irq = irq;
    }
    if (data->irq <= 0) {
        dev_info(dev, "no IRQ configured; use %ums polling\n", data->poll_ms);
        return 0;
    }
    return devm_request_threaded_irq(dev, data->irq, NULL, cst836_interrupt,
        IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "cst836", data);
}

static int cst836_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct cst836_data *data;
    u8 identity[4];
    int result;

    (void)id;
    if (client->addr != CST836_ADDRESS || !i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        return -ENODEV;
    }
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data) {
        return -ENOMEM;
    }
    data->client = client;
    mutex_init(&data->lock);
    INIT_DELAYED_WORK(&data->poll_work, cst836_poll);
    i2c_set_clientdata(client, data);
    result = cst836_read_options(data);
    if (result != 0) {
        return result;
    }
    result = cst836_power_on(data);
    if (result != 0) {
        return result;
    }
    result = cst836_read(client, 0xa7, identity, sizeof(identity));
    if (result != 0) {
        return result;
    }
    if (identity[0] != 0 || identity[3] != CST836_FACTORY_ID) {
        dev_err(&client->dev, "unexpected CST836 identity: %02x/%02x\n", identity[0], identity[3]);
        return -ENODEV;
    }
    result = cst836_register_input(data);
    if (result != 0) {
        return result;
    }
    result = cst836_request_interrupt(data);
    if (result != 0) {
        return result;
    }
    result = devm_add_action_or_reset(&client->dev, cst836_stop, data);
    if (result != 0) {
        return result;
    }
    mutex_lock(&data->lock);
    data->running = true;
    mutex_unlock(&data->lock);
    mod_delayed_work(system_wq, &data->poll_work, 0);
    dev_info(&client->dev, "CST836 factory=0x13 fw=0x%02x, %ux%u, IRQ=%d, poll=%ums\n",
        identity[2], data->width, data->height, data->irq, data->poll_ms);
    return 0;
}

static int cst836_remove(struct i2c_client *client)
{
    struct cst836_data *data = i2c_get_clientdata(client);

    cst836_stop(data);
    dev_info(&client->dev, "removed: irq=%llu reports=%llu contacts=%llu errors=%llu\n",
        data->irq_count, data->reports, data->contacts, data->errors);
    return 0;
}

static int __maybe_unused cst836_suspend(struct device *dev)
{
    cst836_stop(i2c_get_clientdata(to_i2c_client(dev)));
    return 0;
}

static int __maybe_unused cst836_resume(struct device *dev)
{
    struct cst836_data *data = i2c_get_clientdata(to_i2c_client(dev));

    mutex_lock(&data->lock);
    data->running = true;
    data->failures = 0;
    mutex_unlock(&data->lock);
    if (data->irq > 0) {
        enable_irq(data->irq);
    }
    mod_delayed_work(system_wq, &data->poll_work, 0);
    return 0;
}

static void cst836_shutdown(struct i2c_client *client)
{
    cst836_stop(i2c_get_clientdata(client));
}

static SIMPLE_DEV_PM_OPS(cst836_pm_ops, cst836_suspend, cst836_resume);

static const struct of_device_id cst836_of_match[] = {
    {.compatible = "hynitron,cst836"},
    {}
};
MODULE_DEVICE_TABLE(of, cst836_of_match);

static const struct i2c_device_id cst836_ids[] = {
    {"cst836", 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, cst836_ids);

static struct i2c_driver cst836_driver = {
    .driver = {
        .name = "cst836",
        .of_match_table = cst836_of_match,
        .pm = &cst836_pm_ops,
    },
    .probe = cst836_probe,
    .remove = cst836_remove,
    .shutdown = cst836_shutdown,
    .id_table = cst836_ids,
};
module_i2c_driver(cst836_driver);

MODULE_DESCRIPTION("HXM035002/DX757 CST836 I2C touchscreen driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
