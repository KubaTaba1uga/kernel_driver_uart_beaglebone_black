// SPDX-License-Identifier: GPL-2.0
/* ########################################################### */
/* #                    Imports                              # */
/* ########################################################### */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <uapi/linux/serial_reg.h>

/* ########################################################### */
/* #                    Static declarations                  # */
/* ########################################################### */
static struct serial_dev_data {
  void __iomem *regs;
};

static int serial_probe(struct platform_device *pdev);
static int serial_remove(struct platform_device *pdev);
static u32 serial_read(struct serial_dev_data *serial_data,
                       unsigned int offset);

static void serial_write(struct serial_dev_data *serial_data, u32 val,
                         unsigned int offset);
static void init_ti_proc_specific_settings(struct platform_device *pdev);
static void destroy_ti_proc_specific_settings(struct platform_device *pdev);
static int serial_configure_baud_rate(struct platform_device *pdev,
                                      struct serial_dev_data *serial_data);
static void serial_write_char(struct serial_dev_data *serial_data, char c);

/* ########################################################### */
/* #                    Public API                           # */
/* ########################################################### */
MODULE_LICENSE("GPL");

static const struct of_device_id serial_of_match[] = {
    {.compatible = "bootlin,serial"},
    {},
};
MODULE_DEVICE_TABLE(of, serial_of_match);

static struct platform_driver serial_driver = {
    .driver =
        {
            .name = "serial",
            .owner = THIS_MODULE,
            .of_match_table = of_match_ptr(serial_of_match),
        },
    .probe = serial_probe,
    .remove = serial_remove,
};
module_platform_driver(serial_driver);

/* ########################################################### */
/* #                    Private API                          # */
/* ########################################################### */

int serial_probe(struct platform_device *pdev) {
  struct serial_dev_data *serial_data;
  int err;

  pr_info("Called %s\n", __func__);

  serial_data = devm_kzalloc(&pdev->dev, sizeof(*serial_data), GFP_KERNEL);
  if (!serial_data) {
    return -ENOMEM;
  }

  serial_data->regs = devm_platform_ioremap_resource(pdev, 0);
  if (IS_ERR(serial_data->regs)) {
    return PTR_ERR(serial_data->regs);
  }

  init_ti_proc_specific_settings(pdev);

  err = serial_configure_baud_rate(pdev, serial_data);
  if (err) {
    goto cleanup;
  }

  serial_write_char(serial_data, 'x');

  return 0;

cleanup:
  destroy_ti_proc_specific_settings(pdev);
  return err;
}

int serial_remove(struct platform_device *pdev) {
  pr_info("Called %s\n", __func__);

  destroy_ti_proc_specific_settings(pdev);

  return 0;
}

u32 serial_read(struct serial_dev_data *serial_data, unsigned int offset) {
  return readl(serial_data->regs + (offset * 4));
}

void serial_write(struct serial_dev_data *serial_data, u32 val,
                  unsigned int offset) {
  writel(val, serial_data->regs + (offset * 4));
}

void init_ti_proc_specific_settings(struct platform_device *pdev) {
  pm_runtime_enable(&pdev->dev);
  pm_runtime_get_sync(&pdev->dev);
}

void destroy_ti_proc_specific_settings(struct platform_device *pdev) {
  pm_runtime_disable(&pdev->dev);
}

int serial_configure_baud_rate(struct platform_device *pdev,
                               struct serial_dev_data *serial_data) {
  unsigned int uartclk, baud_divisor;
  int err;

  err = of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);
  if (err) {
    // It is better than pr_err because if many devices uses the same driver,
    // prints where it camed from.
    dev_err(&pdev->dev, "clock-frequency property not found in Device Tree\n");
    return err;
  }

  baud_divisor = uartclk / 16 / 115200;
  serial_write(serial_data, 0x07, UART_OMAP_MDR1);
  serial_write(serial_data, 0x00, UART_LCR);
  serial_write(serial_data, UART_LCR_DLAB, UART_LCR);
  serial_write(serial_data, baud_divisor & 0xff, UART_DLL);
  serial_write(serial_data, (baud_divisor >> 8) & 0xff, UART_DLM);
  serial_write(serial_data, UART_LCR_WLEN8, UART_LCR);
  serial_write(serial_data, 0x00, UART_OMAP_MDR1);

  /* Clear UART FIFOs */
  serial_write(serial_data, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT,
               UART_FCR);

  return 0;
}

void serial_write_char(struct serial_dev_data *serial_data, char c) {
  while (!(serial_read(serial_data, UART_LSR) & UART_LSR_THRE)) {
    cpu_relax();
  }

  serial_write(serial_data, c, UART_TX);
}
