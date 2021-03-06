/*
 * Copyright (C) 2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Notes:
 * 1. We avoid using a stack-allocated buffer for SPI messages. Using
 *    a kmalloced buffer is probably better, given we shouldn't assume
 *    any particular usage by SPI core.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nand.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>

/* SPI NAND commands */
#define	SPI_NAND_WRITE_ENABLE		0x06
#define	SPI_NAND_WRITE_DISABLE		0x04
#define	SPI_NAND_GET_FEATURE		0x0f
#define	SPI_NAND_SET_FEATURE		0x1f
#define	SPI_NAND_PAGE_READ		0x13
#define	SPI_NAND_READ_CACHE		0x03
#define	SPI_NAND_FAST_READ_CACHE	0x0b
#define	SPI_NAND_READ_CACHE_X2		0x3b
#define	SPI_NAND_READ_CACHE_X4		0x6b
#define	SPI_NAND_READ_CACHE_DUAL_IO	0xbb
#define	SPI_NAND_READ_CACHE_QUAD_IO	0xeb
#define	SPI_NAND_READ_ID		0x9f
#define	SPI_NAND_PROGRAM_LOAD		0x02
#define	SPI_NAND_PROGRAM_LOAD4		0x32
#define	SPI_NAND_PROGRAM_EXEC		0x10
#define	SPI_NAND_PROGRAM_LOAD_RANDOM	0x84
#define	SPI_NAND_PROGRAM_LOAD_RANDOM4	0xc4
#define	SPI_NAND_BLOCK_ERASE		0xd8
#define	SPI_NAND_RESET			0xff

#define SPI_NAND_GD5F_READID_LEN	2
#define SPI_NAND_MT29F_READID_LEN	2

#define SPI_NAND_GD5F_ECC_MASK		(BIT(0) | BIT(1) | BIT(2))
#define SPI_NAND_GD5F_ECC_UNCORR	(BIT(0) | BIT(1) | BIT(2))
#define SPI_NAND_GD5F_ECC_SHIFT		4

#define SPI_NAND_MT29F_ECC_MASK		(BIT(0) | BIT(1))
#define SPI_NAND_MT29F_ECC_UNCORR	(BIT(1))
#define SPI_NAND_MT29F_ECC_SHIFT		4

static struct nand_ecclayout ecc_layout_gd5f = {
	.eccbytes = 128,
	.eccpos = {
		128, 129, 130, 131, 132, 133, 134, 135,
		136, 137, 138, 139, 140, 141, 142, 143,
		144, 145, 146, 147, 148, 149, 150, 151,
		152, 153, 154, 155, 156, 157, 158, 159,
		160, 161, 162, 163, 164, 165, 166, 167,
		168, 169, 170, 171, 172, 173, 174, 175,
		176, 177, 178, 179, 180, 181, 182, 183,
		184, 185, 186, 187, 188, 189, 190, 191,
		192, 193, 194, 195, 196, 197, 198, 199,
		200, 201, 202, 203, 204, 205, 206, 207,
		208, 209, 210, 211, 212, 213, 214, 215,
		216, 217, 218, 219, 220, 221, 222, 223,
		224, 225, 226, 227, 228, 229, 230, 231,
		232, 233, 234, 235, 236, 237, 238, 239,
		240, 241, 242, 243, 244, 245, 246, 247,
		248, 249, 250, 251, 252, 253, 254, 255
	},
	.oobfree = { {1, 127} }
};

static struct nand_ecclayout ecc_layout_mt29f = {
	.eccbytes = 32,
	.eccpos = {
		8, 9, 10, 11, 12, 13, 14, 15,
		24, 25, 26, 27, 28, 29, 30, 31,
		40, 41, 42, 43, 44, 45, 46, 47,
		56, 57, 58, 59, 60, 61, 62, 63,
	 },
};

static struct nand_flash_dev spi_nand_flash_ids[] = {
	{
		.name = "SPI NAND 512MiB 3,3V",
		.id = { NAND_MFR_GIGADEVICE, 0xb4 },
		.chipsize = 512,
		.pagesize = SZ_4K,
		.erasesize = SZ_256K,
		.id_len = 2,
		.oobsize = 256,
		.ecc.strength_ds = 8,
		.ecc.step_ds = 512,
		.ecc.layout = &ecc_layout_gd5f,
	},
	{
		.name = "SPI NAND 512MiB 1,8V",
		.id = { NAND_MFR_GIGADEVICE, 0xa4 },
		.chipsize = 512,
		.pagesize = SZ_4K,
		.erasesize = SZ_256K,
		.id_len = 2,
		.oobsize = 256,
		.ecc.strength_ds = 8,
		.ecc.step_ds = 512,
		.ecc.layout = &ecc_layout_gd5f,
	},
	{
		.name = "SPI NAND 512MiB 3,3V",
		.id = { NAND_MFR_MICRON, 0x32 },
		.chipsize = 512,
		.pagesize = SZ_2K,
		.erasesize = SZ_128K,
		.id_len = 2,
		.oobsize = 64,
		.ecc.strength_ds = 4,
		.ecc.step_ds = 512,
		.ecc.layout = &ecc_layout_mt29f,
	},
	{
		.name = "SPI NAND 256MiB 3,3V",
		.id = { NAND_MFR_MICRON, 0x22 },
		.chipsize = 256,
		.pagesize = SZ_2K,
		.erasesize = SZ_128K,
		.id_len = 2,
		.oobsize = 64,
		.ecc.strength_ds = 4,
		.ecc.step_ds = 512,
		.ecc.layout = &ecc_layout_mt29f,
	},
};

enum spi_nand_device_variant {
	SPI_NAND_GENERIC,
	SPI_NAND_MT29F,
	SPI_NAND_GD5F,
};

struct spi_nand_device_cmd {

	/*
	 * Command and address. I/O errors have been observed if a
	 * separate spi_transfer is used for command and address,
	 * so keep them together.
	 */
	u32 n_cmd;
	u8 cmd[5];

	/* Tx data */
	u32 n_tx;
	u8 *tx_buf;

	/* Rx data */
	u32 n_rx;
	u8 *rx_buf;
	u8 rx_nbits;
	u8 tx_nbits;
};

struct spi_nand_device {
	struct spi_nand	spi_nand;
	struct spi_device *spi;

	struct spi_nand_device_cmd cmd;
};

static int spi_nand_send_command(struct spi_device *spi,
				 struct spi_nand_device_cmd *cmd)
{
	struct spi_message message;
	struct spi_transfer x[2];

	if (!cmd->n_cmd) {
		dev_err(&spi->dev, "cannot send an empty command\n");
		return -EINVAL;
	}

	if (cmd->n_tx && cmd->n_rx) {
		dev_err(&spi->dev, "cannot send and receive data at the same time\n");
		return -EINVAL;
	}

	spi_message_init(&message);
	memset(x, 0, sizeof(x));

	/* Command and address */
	x[0].len = cmd->n_cmd;
	x[0].tx_buf = cmd->cmd;
	x[0].tx_nbits = cmd->tx_nbits;
	spi_message_add_tail(&x[0], &message);

	/* Data to be transmitted */
	if (cmd->n_tx) {
		x[1].len = cmd->n_tx;
		x[1].tx_buf = cmd->tx_buf;
		x[1].tx_nbits = cmd->tx_nbits;
		spi_message_add_tail(&x[1], &message);
	}

	/* Data to be received */
	if (cmd->n_rx) {
		x[1].len = cmd->n_rx;
		x[1].rx_buf = cmd->rx_buf;
		x[1].rx_nbits = cmd->rx_nbits;
		spi_message_add_tail(&x[1], &message);
	}

	return spi_sync(spi, &message);
}

static int spi_nand_device_reset(struct spi_nand *snand)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 1;
	cmd->cmd[0] = SPI_NAND_RESET;

	dev_dbg(snand->dev, "%s\n", __func__);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_read_reg(struct spi_nand *snand, u8 opcode, u8 *buf)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 2;
	cmd->cmd[0] = SPI_NAND_GET_FEATURE;
	cmd->cmd[1] = opcode;
	cmd->n_rx = 1;
	cmd->rx_buf = buf;

	dev_dbg(snand->dev, "%s: reg 0%x\n", __func__, opcode);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_write_reg(struct spi_nand *snand, u8 opcode, u8 *buf)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 2;
	cmd->cmd[0] = SPI_NAND_SET_FEATURE;
	cmd->cmd[1] = opcode;
	cmd->n_tx = 1;
	cmd->tx_buf = buf;

	dev_dbg(snand->dev, "%s: reg 0%x\n", __func__, opcode);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_write_enable(struct spi_nand *snand)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 1;
	cmd->cmd[0] = SPI_NAND_WRITE_ENABLE;

	dev_dbg(snand->dev, "%s\n", __func__);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_write_disable(struct spi_nand *snand)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 1;
	cmd->cmd[0] = SPI_NAND_WRITE_DISABLE;

	dev_dbg(snand->dev, "%s\n", __func__);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_write_page(struct spi_nand *snand,
				      unsigned int page_addr)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 4;
	cmd->cmd[0] = SPI_NAND_PROGRAM_EXEC;
	cmd->cmd[1] = (u8)((page_addr & 0xff0000) >> 16);
	cmd->cmd[2] = (u8)((page_addr & 0xff00) >> 8);
	cmd->cmd[3] = (u8)(page_addr & 0xff);

	dev_dbg(snand->dev, "%s: page 0x%x\n", __func__, page_addr);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_store_cache(struct spi_nand *snand,
				       unsigned int page_offset, size_t length,
				       u8 *write_buf)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;
	struct spi_device *spi = snand_dev->spi;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 3;
	cmd->cmd[0] = spi->mode & SPI_TX_QUAD ? SPI_NAND_PROGRAM_LOAD4 :
			SPI_NAND_PROGRAM_LOAD;
	cmd->cmd[1] = (u8)((page_offset & 0xff00) >> 8);
	cmd->cmd[2] = (u8)(page_offset & 0xff);
	cmd->n_tx = length;
	cmd->tx_buf = write_buf;
	cmd->tx_nbits = spi->mode & SPI_TX_QUAD ? 4 : 1;

	dev_dbg(snand->dev, "%s: offset 0x%x\n", __func__, page_offset);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_load_page(struct spi_nand *snand,
				     unsigned int page_addr)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 4;
	cmd->cmd[0] = SPI_NAND_PAGE_READ;
	cmd->cmd[1] = (u8)((page_addr & 0xff0000) >> 16);
	cmd->cmd[2] = (u8)((page_addr & 0xff00) >> 8);
	cmd->cmd[3] = (u8)(page_addr & 0xff);

	dev_dbg(snand->dev, "%s: page 0x%x\n", __func__, page_addr);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_read_cache(struct spi_nand *snand,
				      unsigned int page_offset, size_t length,
				      u8 *read_buf)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;
	struct spi_device *spi = snand_dev->spi;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	if ((spi->mode & SPI_RX_DUAL) || (spi->mode & SPI_RX_QUAD))
		cmd->n_cmd = 5;
	else
		cmd->n_cmd = 4;
	cmd->cmd[0] = (spi->mode & SPI_RX_QUAD) ? SPI_NAND_READ_CACHE_X4 :
			((spi->mode & SPI_RX_DUAL) ? SPI_NAND_READ_CACHE_X2 :
			SPI_NAND_READ_CACHE);
	cmd->cmd[1] = 0; /* dummy byte */
	cmd->cmd[2] = (u8)((page_offset & 0xff00) >> 8);
	cmd->cmd[3] = (u8)(page_offset & 0xff);
	cmd->cmd[4] = 0; /* dummy byte */
	cmd->n_rx = length;
	cmd->rx_buf = read_buf;
	cmd->rx_nbits = (spi->mode & SPI_RX_QUAD) ? 4 :
			((spi->mode & SPI_RX_DUAL) ? 2 : 1);

	dev_dbg(snand->dev, "%s: offset 0x%x\n", __func__, page_offset);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_device_block_erase(struct spi_nand *snand,
				       unsigned int page_addr)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 4;
	cmd->cmd[0] = SPI_NAND_BLOCK_ERASE;
	cmd->cmd[1] = (u8)((page_addr & 0xff0000) >> 16);
	cmd->cmd[2] = (u8)((page_addr & 0xff00) >> 8);
	cmd->cmd[3] = (u8)(page_addr & 0xff);

	dev_dbg(snand->dev, "%s: block 0x%x\n", __func__, page_addr);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_gd5f_read_id(struct spi_nand *snand, u8 *buf)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 1;
	cmd->cmd[0] = SPI_NAND_READ_ID;
	cmd->n_rx = SPI_NAND_GD5F_READID_LEN;
	cmd->rx_buf = buf;

	dev_dbg(snand->dev, "%s\n", __func__);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static int spi_nand_mt29f_read_id(struct spi_nand *snand, u8 *buf)
{
	struct spi_nand_device *snand_dev = snand->priv;
	struct spi_nand_device_cmd *cmd = &snand_dev->cmd;

	memset(cmd, 0, sizeof(struct spi_nand_device_cmd));
	cmd->n_cmd = 1;
	cmd->cmd[0] = SPI_NAND_READ_ID;
	cmd->n_rx = SPI_NAND_MT29F_READID_LEN;
	cmd->rx_buf = buf;

	dev_dbg(snand->dev, "%s\n", __func__);

	return spi_nand_send_command(snand_dev->spi, cmd);
}

static void spi_nand_mt29f_ecc_status(unsigned int status,
				      unsigned int *corrected,
				      unsigned int *ecc_error)
{
	unsigned int ecc_status = (status >> SPI_NAND_MT29F_ECC_SHIFT) &
					     SPI_NAND_MT29F_ECC_MASK;

	*ecc_error = (ecc_status == SPI_NAND_MT29F_ECC_UNCORR) ? 1 : 0;
	if (*ecc_error == 0)
		*corrected = ecc_status;
}

static void spi_nand_gd5f_ecc_status(unsigned int status,
				     unsigned int *corrected,
				     unsigned int *ecc_error)
{
	unsigned int ecc_status = (status >> SPI_NAND_GD5F_ECC_SHIFT) &
					     SPI_NAND_GD5F_ECC_MASK;

	*ecc_error = (ecc_status == SPI_NAND_GD5F_ECC_UNCORR) ? 1 : 0;
	if (*ecc_error == 0)
		*corrected = (ecc_status > 1) ? (2 + ecc_status) : 0;
}

static int spi_nand_device_probe(struct spi_device *spi)
{
	enum spi_nand_device_variant variant;
	struct spi_nand_device *priv;
	struct spi_nand *snand;
	int ret;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	snand = &priv->spi_nand;

	snand->read_cache = spi_nand_device_read_cache;
	snand->load_page = spi_nand_device_load_page;
	snand->store_cache = spi_nand_device_store_cache;
	snand->write_page = spi_nand_device_write_page;
	snand->write_reg = spi_nand_device_write_reg;
	snand->read_reg = spi_nand_device_read_reg;
	snand->block_erase = spi_nand_device_block_erase;
	snand->reset = spi_nand_device_reset;
	snand->write_enable = spi_nand_device_write_enable;
	snand->write_disable = spi_nand_device_write_disable;
	snand->dev = &spi->dev;
	snand->priv = priv;

	/*
	 * gd5f reads three ID bytes, and mt29f reads one dummy address byte
	 * and two ID bytes. Therefore, we could detect both in the same
	 * read_id implementation by reading _with_ and _without_ a dummy byte,
	 * until a proper manufacturer is found.
	 *
	 * This'll mean we won't need to specify any specific compatible string
	 * for a given device, and instead just support spi-nand.
	 */
	variant = spi_get_device_id(spi)->driver_data;
	switch (variant) {
	case SPI_NAND_MT29F:
		snand->read_id = spi_nand_mt29f_read_id;
		snand->get_ecc_status = spi_nand_mt29f_ecc_status;
		break;
	case SPI_NAND_GD5F:
		snand->read_id = spi_nand_gd5f_read_id;
		snand->get_ecc_status = spi_nand_gd5f_ecc_status;
		break;
	default:
		dev_err(snand->dev, "unknown device\n");
		return -ENODEV;
	}

	spi_set_drvdata(spi, snand);
	priv->spi = spi;

	ret = spi_nand_register(snand, spi_nand_flash_ids);
	if (ret)
		return ret;
	return 0;
}

static int spi_nand_device_remove(struct spi_device *spi)
{
	struct spi_nand *snand = spi_get_drvdata(spi);

	spi_nand_unregister(snand);

	return 0;
}

const struct spi_device_id spi_nand_id_table[] = {
	{ "spi-nand", SPI_NAND_GENERIC },
	{ "mt29f", SPI_NAND_MT29F },
	{ "gd5f", SPI_NAND_GD5F },
	{ },
};
MODULE_DEVICE_TABLE(spi, spi_nand_id_table);

static struct spi_driver spi_nand_device_driver = {
	.driver = {
		.name	= "spi_nand_device",
		.owner	= THIS_MODULE,
	},
	.id_table = spi_nand_id_table,
	.probe	= spi_nand_device_probe,
	.remove	= spi_nand_device_remove,
};
module_spi_driver(spi_nand_device_driver);

MODULE_AUTHOR("Ezequiel Garcia <ezequiel.garcia@imgtec.com>");
MODULE_DESCRIPTION("SPI NAND device support");
MODULE_LICENSE("GPL v2");
