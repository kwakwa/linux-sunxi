/*
 * Allwinner SUNXI "glue layer"
 *
 * Copyright � 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * Based on the sw_usb "Allwinner OTG Dual Role Controller" code.
 *  Copyright 2007-2012 (C) Allwinner Technology Co., Ltd.
 *  javen <javen@allwinnertech.com>
 *
 * Based on the DA8xx "glue layer" code.
 *  Copyright (c) 2008-2009 MontaVista Software, Inc. <source@mvista.com>
 *  Copyright (C) 2005-2006 by Texas Instruments
 *
 * This file is part of the Inventra Controller Driver for Linux.
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 *
 */
#define DEBUG 1
#define VERBOSE 1
#define pr_dbg pr_debug

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <plat/sys_config.h>
#include <mach/clock.h>
#include "../../power/axp_power/axp-gpio.h"

#include "musb_core.h"

#define SUNXI_MUSB_DRIVER_NAME "sunxi_musb"

struct sw_hcd_io {
	struct clk	*sie_clk;		/* SIE clock handle	*/
	struct clk	*phy_clk;		/* PHY gate		*/
	struct clk	*phy0_clk;		/* PHY reset		*/

	unsigned int	Drv_vbus_Handle;
	user_gpio_set_t	drv_vbus_gpio_set;
	int		vbus_on;
};

struct sunxi_musb_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct sw_hcd_io	io;

	int exiting;
};

static inline struct sunxi_musb_glue *musb_to_glue(struct musb *musb)
{
	struct device *sunxi_musb_dev = musb->controller->parent;
	struct sunxi_musb_glue *glue = dev_get_drvdata(sunxi_musb_dev);

	return glue;
}

/******************************************************************************
 ******************************************************************************
 * From the Allwinner driver
 ******************************************************************************
 ******************************************************************************/

/******************************************************************************
 * From include/sunxi_usb_bsp.h
 ******************************************************************************/

/* reg offsets */
#define  USBC_REG_o_ISCR	0x0400
#define  USBC_REG_o_PHYCTL	0x0404
#define  USBC_REG_o_PHYBIST	0x0408
#define  USBC_REG_o_PHYTUNE	0x040c

#define  USBC_REG_o_VEND0	0x0043

/* Interface Status and Control */
#define  USBC_BP_ISCR_VBUS_VALID_FROM_DATA	30
#define  USBC_BP_ISCR_VBUS_VALID_FROM_VBUS	29
#define  USBC_BP_ISCR_EXT_ID_STATUS		28
#define  USBC_BP_ISCR_EXT_DM_STATUS		27
#define  USBC_BP_ISCR_EXT_DP_STATUS		26
#define  USBC_BP_ISCR_MERGED_VBUS_STATUS	25
#define  USBC_BP_ISCR_MERGED_ID_STATUS		24

#define  USBC_BP_ISCR_ID_PULLUP_EN		17
#define  USBC_BP_ISCR_DPDM_PULLUP_EN		16
#define  USBC_BP_ISCR_FORCE_ID			14
#define  USBC_BP_ISCR_FORCE_VBUS_VALID		12
#define  USBC_BP_ISCR_VBUS_VALID_SRC		10

#define  USBC_BP_ISCR_HOSC_EN			7
#define  USBC_BP_ISCR_VBUS_CHANGE_DETECT	6
#define  USBC_BP_ISCR_ID_CHANGE_DETECT		5
#define  USBC_BP_ISCR_DPDM_CHANGE_DETECT	4
#define  USBC_BP_ISCR_IRQ_ENABLE		3
#define  USBC_BP_ISCR_VBUS_CHANGE_DETECT_EN	2
#define  USBC_BP_ISCR_ID_CHANGE_DETECT_EN	1
#define  USBC_BP_ISCR_DPDM_CHANGE_DETECT_EN	0

/* usb id type */
#define  USBC_ID_TYPE_DISABLE		0
#define  USBC_ID_TYPE_HOST		1
#define  USBC_ID_TYPE_DEVICE		2

/* usb vbus valid type */
#define  USBC_VBUS_TYPE_DISABLE		0
#define  USBC_VBUS_TYPE_LOW		1
#define  USBC_VBUS_TYPE_HIGH		2

/* usb io type */
#define  USBC_IO_TYPE_PIO		0
#define  USBC_IO_TYPE_DMA		1

/* usb ep type */
#define  USBC_EP_TYPE_IDLE		0
#define  USBC_EP_TYPE_EP0		1
#define  USBC_EP_TYPE_TX		2
#define  USBC_EP_TYPE_RX		3

/* vendor0 */
#define  USBC_BP_VEND0_DRQ_SEL		1
#define  USBC_BP_VEND0_BUS_SEL		0

/******************************************************************************
 * From usbc/usbc.c
 ******************************************************************************/

static u32 __USBC_WakeUp_ClearChangeDetect(u32 reg_val)
{
	u32 temp = reg_val;

	temp &= ~(1 << USBC_BP_ISCR_VBUS_CHANGE_DETECT);
	temp &= ~(1 << USBC_BP_ISCR_ID_CHANGE_DETECT);
	temp &= ~(1 << USBC_BP_ISCR_DPDM_CHANGE_DETECT);

	return temp;
}

void USBC_EnableIdPullUp(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val |= (1 << USBC_BP_ISCR_ID_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

void USBC_DisableIdPullUp(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(1 << USBC_BP_ISCR_ID_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void USBC_EnableDpDmPullUp(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val |= (1 << USBC_BP_ISCR_DPDM_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void USBC_DisableDpDmPullUp(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(1 << USBC_BP_ISCR_DPDM_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void __USBC_ForceIdDisable(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void __USBC_ForceIdToLow(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val |= (0x02 << USBC_BP_ISCR_FORCE_ID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void __USBC_ForceIdToHigh(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val |= (0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

/* force id to (id_type) */
static void USBC_ForceId(__iomem void *base, u32 id_type)
{
	pr_dbg("%s(): id_type %s\n", __func__,
		id_type == USBC_ID_TYPE_HOST ? "host" :
		(id_type == USBC_ID_TYPE_DEVICE ? "device" : "disable"));

	switch (id_type) {
	case USBC_ID_TYPE_HOST:
		__USBC_ForceIdToLow(base);
		break;

	case USBC_ID_TYPE_DEVICE:
		__USBC_ForceIdToHigh(base);
		break;

	default:
		__USBC_ForceIdDisable(base);
		break;
	}
}

static void __USBC_ForceVbusValidDisable(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void __USBC_ForceVbusValidToLow(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val |= (0x02 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

static void __USBC_ForceVbusValidToHigh(__iomem void *base)
{
	u32 reg_val;

	reg_val = musb_readl(base, USBC_REG_o_ISCR);
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val |= (0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	musb_writel(base, USBC_REG_o_ISCR, reg_val);
}

/* force vbus valid to (id_type) */
static void USBC_ForceVbusValid(__iomem void *base, u32 vbus_type)
{
	pr_dbg("%s(): vbus_type %s\n", __func__,
		vbus_type == USBC_VBUS_TYPE_LOW ? "low" :
		(vbus_type == USBC_VBUS_TYPE_HIGH ? "high" : "disable"));

	switch (vbus_type) {
	case USBC_VBUS_TYPE_LOW:
		__USBC_ForceVbusValidToLow(base);
		break;

	case USBC_VBUS_TYPE_HIGH:
		__USBC_ForceVbusValidToHigh(base);
		break;

	default:
		__USBC_ForceVbusValidDisable(base);
		break;
	}
}

static void USBC_SelectBus(__iomem void *base, __u32 io_type, __u32 ep_type,
			   __u32 ep_index)
{
	u32 reg_val = 0;

	reg_val = musb_readb(base, USBC_REG_o_VEND0);

	if (io_type == USBC_IO_TYPE_DMA) {
		if (ep_type == USBC_EP_TYPE_TX) {
			/* drq_sel */
			reg_val |= ((ep_index - 1) << 1) <<
					USBC_BP_VEND0_DRQ_SEL;
			/* io_dma */
			reg_val |= 1 << USBC_BP_VEND0_BUS_SEL;
		} else {
			reg_val |= ((ep_index << 1) - 1) <<
					USBC_BP_VEND0_DRQ_SEL;
			reg_val |= 1 << USBC_BP_VEND0_BUS_SEL;
		}
	} else {
		/* Clear drq_sel, select pio */
		reg_val &= 0x00;
	}

	musb_writeb(base, USBC_REG_o_VEND0, reg_val);
}

/******************************************************************************
 * From usbc/usbc_phy.c
 ******************************************************************************/

static u32 USBC_Phy_TpWrite(__iomem void *base, u32 usbc_no, u32 addr, u32 data,
			    u32 len)
{
	u32 temp = 0, dtmp = 0;
	u32 j = 0;

	dtmp = data;
	for (j = 0; j < len; j++) {
		/* set the bit address to be write */
		temp = musb_readl(base, USBC_REG_o_PHYCTL);
		temp &= ~(0xff << 8);
		temp |= ((addr + j) << 8);
		musb_writel(base, USBC_REG_o_PHYCTL, temp);

		temp = musb_readb(base, USBC_REG_o_PHYCTL);
		temp &= ~(0x1 << 7);
		temp |= (dtmp & 0x1) << 7;
		temp &= ~(0x1 << (usbc_no << 1));
		musb_writeb(base, USBC_REG_o_PHYCTL, temp);

		temp = musb_readb(base, USBC_REG_o_PHYCTL);
		temp |= (0x1 << (usbc_no << 1));
		musb_writeb(base, USBC_REG_o_PHYCTL, temp);

		temp = musb_readb(base, USBC_REG_o_PHYCTL);
		temp &= ~(0x1 << (usbc_no << 1));
		musb_writeb(base, USBC_REG_o_PHYCTL, temp);
		dtmp >>= 1;
	}

	return data;
}

static u32 USBC_Phy_Write(__iomem void *base, u32 usbc_no, u32 addr, u32 data,
			  u32 len)
{
	return USBC_Phy_TpWrite(base, usbc_no, addr, data, len);
}

static void UsbPhyInit(__iomem void *base, u32 usbc_no)
{
	pr_dbg("%s():\n", __func__);

	/* NOTE: comments google-translated. */

	/* Regulation 45 ohms */
	if (usbc_no == 0)
		USBC_Phy_Write(base, usbc_no, 0x0c, 0x01, 1);

	/* Adjust the magnitude and rate of USB0 PHY */
	USBC_Phy_Write(base, usbc_no, 0x20, 0x14, 5);

	/* Adjust the disconnect threshold */
#ifdef CONFIG_ARCH_SUN4I
	USBC_Phy_Write(base, usbc_no, 0x2a, 3, 2);
#else
	USBC_Phy_Write(base, usbc_no, 0x2a, 2, 2);
#endif

	return;
}

/******************************************************************************
 * From hcd/hcd0/sw_hcd0.c
 ******************************************************************************/

static s32 usb_clock_init(struct sunxi_musb_glue *glue)
{
	int err = 0;
	struct sw_hcd_io *sw_hcd_io = &glue->io;

	pr_dbg("%s():\n", __func__);

	sw_hcd_io->sie_clk = NULL;
	sw_hcd_io->phy_clk = NULL;
	sw_hcd_io->phy0_clk = NULL;

	sw_hcd_io->sie_clk = clk_get(NULL, "ahb_usb0");
	if (IS_ERR(sw_hcd_io->sie_clk)) {
		dev_err(glue->dev, "get usb sie clk failed.\n");
		err = PTR_ERR(sw_hcd_io->sie_clk);
		goto failed;
	}

	sw_hcd_io->phy_clk = clk_get(NULL, "usb_phy");
	if (IS_ERR(sw_hcd_io->phy_clk)) {
		dev_err(glue->dev, "get usb phy clk failed.\n");
		err = PTR_ERR(sw_hcd_io->phy_clk);
		goto failed;
	}

	sw_hcd_io->phy0_clk = clk_get(NULL, "usb_phy0");
	if (IS_ERR(sw_hcd_io->phy0_clk)) {
		dev_err(glue->dev, "get usb phy0 clk failed.\n");
		err = PTR_ERR(sw_hcd_io->phy0_clk);
		goto failed;
	}

	return 0;

failed:
	if (sw_hcd_io->sie_clk) {
		clk_put(sw_hcd_io->sie_clk);
		sw_hcd_io->sie_clk = NULL;
	}

	if (sw_hcd_io->phy_clk) {
		clk_put(sw_hcd_io->phy_clk);
		sw_hcd_io->phy_clk = NULL;
	}

	if (sw_hcd_io->phy0_clk) {
		clk_put(sw_hcd_io->phy0_clk);
		sw_hcd_io->phy0_clk = NULL;
	}

	return err;
}

static s32 usb_clock_exit(struct sunxi_musb_glue *glue)
{
	struct sw_hcd_io *sw_hcd_io = &glue->io;

	pr_dbg("%s():\n", __func__);

	if (sw_hcd_io->sie_clk) {
		clk_put(sw_hcd_io->sie_clk);
		sw_hcd_io->sie_clk = NULL;
	}

	if (sw_hcd_io->phy_clk) {
		clk_put(sw_hcd_io->phy_clk);
		sw_hcd_io->phy_clk = NULL;
	}

	if (sw_hcd_io->phy0_clk) {
		clk_put(sw_hcd_io->phy0_clk);
		sw_hcd_io->phy0_clk = NULL;
	}

	return 0;
}

static s32 open_usb_clock(struct sunxi_musb_glue *glue)
{
	struct sw_hcd_io *sw_hcd_io = &glue->io;
	int ret;

	pr_dbg("%s():\n", __func__);

	ret = clk_enable(sw_hcd_io->sie_clk);
	if (ret < 0) {
		dev_err(glue->dev, "could not enable sie_clk\n");
		return ret;
	}

	mdelay(10);

	ret = clk_enable(sw_hcd_io->phy_clk);
	if (ret < 0) {
		dev_err(glue->dev, "could not enable phy_clk\n");
		return ret;
	}

	ret = clk_enable(sw_hcd_io->phy0_clk);
	if (ret < 0) {
		dev_err(glue->dev, "could not enable pky0_clk\n");
		return ret;
	}

	clk_reset(sw_hcd_io->phy0_clk, 0);
	mdelay(10);

	return 0;
}

static s32 close_usb_clock(struct sunxi_musb_glue *glue)
{
	struct sw_hcd_io *sw_hcd_io = &glue->io;

	pr_dbg("%s():\n", __func__);

	clk_reset(sw_hcd_io->phy0_clk, 1);
	clk_disable(sw_hcd_io->phy0_clk);
	clk_disable(sw_hcd_io->phy_clk);
	clk_disable(sw_hcd_io->sie_clk);

	return 0;
}

static s32 pin_init(struct sunxi_musb_glue *glue)
{
	struct sw_hcd_io *sw_hcd_io = &glue->io;
	s32 ret = 0;

	pr_dbg("%s():\n", __func__);

	sw_hcd_io->vbus_on = 0;

	/* request gpio */
	ret = script_parser_fetch("usbc0", "usb_drv_vbus_gpio",
				  (int *)&sw_hcd_io->drv_vbus_gpio_set, 64);
	if (ret != 0)
		dev_warn(glue->dev, "get usbc0(drv vbus) id failed\n");

	if (!sw_hcd_io->drv_vbus_gpio_set.port) {
		dev_err(glue->dev, "usbc0(drv vbus) is invalid\n");
		sw_hcd_io->Drv_vbus_Handle = 0;
		return 0;
	}

	if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff) { /* power */
		if (sw_hcd_io->drv_vbus_gpio_set.mul_sel == 0 ||
				sw_hcd_io->drv_vbus_gpio_set.mul_sel == 1) {
			axp_gpio_set_io(sw_hcd_io->drv_vbus_gpio_set.port_num,
					sw_hcd_io->drv_vbus_gpio_set.mul_sel);
			axp_gpio_set_value(
					sw_hcd_io->drv_vbus_gpio_set.port_num,
					!sw_hcd_io->drv_vbus_gpio_set.data);

			return 100 + sw_hcd_io->drv_vbus_gpio_set.port_num;
		} else {
			dev_err(glue->dev, "unknown gpio mul_sel(%d)\n",
				sw_hcd_io->drv_vbus_gpio_set.mul_sel);
			return 0;
		}
	} else {  /* axp */
		sw_hcd_io->Drv_vbus_Handle = sunxi_gpio_request_array(
				&sw_hcd_io->drv_vbus_gpio_set, 1);
		if (sw_hcd_io->Drv_vbus_Handle == 0) {
			dev_err(glue->dev, "gpio_request failed\n");
			return -1;
		}

		/* set config, ouput */
		gpio_set_one_pin_io_status(sw_hcd_io->Drv_vbus_Handle,
					   !sw_hcd_io->drv_vbus_gpio_set.data,
					   NULL);

		/* reserved is pull down */
		gpio_set_one_pin_pull(sw_hcd_io->Drv_vbus_Handle, 2, NULL);
	}

	return 0;
}

static s32 pin_exit(struct sunxi_musb_glue *glue)
{
	struct sw_hcd_io *sw_hcd_io = &glue->io;

	pr_dbg("%s():\n", __func__);

	if (sw_hcd_io->Drv_vbus_Handle) {
		if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff) { /* power */
			axp_gpio_set_io(sw_hcd_io->drv_vbus_gpio_set.port_num,
					sw_hcd_io->drv_vbus_gpio_set.mul_sel);
			axp_gpio_set_value(
					sw_hcd_io->drv_vbus_gpio_set.port_num,
					sw_hcd_io->drv_vbus_gpio_set.data);
		} else {
			gpio_release(sw_hcd_io->Drv_vbus_Handle, 0);
		}
	}

	sw_hcd_io->Drv_vbus_Handle = 0;
	sw_hcd_io->vbus_on = 0;

	return 0;
}

static void sw_hcd_board_set_vbus(struct musb *musb, int is_on)
{
	struct sunxi_musb_glue *glue = musb_to_glue(musb);
	struct sw_hcd_io *sw_hcd_io = &glue->io;
	u32 on_off = 0;
	u32 val;

	dev_info(glue->dev, "is_on = %d\n", is_on);

	if (sw_hcd_io->Drv_vbus_Handle == 0) {
		dev_info(glue->dev, "wrn: sw_hcd_io->drv_vbus_Handle is null\n");
		return;
	}

	/* set power */
	on_off = !!is_on;
	if (sw_hcd_io->drv_vbus_gpio_set.data != 0)
		on_off = !on_off; /* inverse */

	if (is_on) {
		if (sw_hcd_io->vbus_on)
			return; /* already enabled */

		sw_hcd_io->vbus_on = 1;

		dev_info(glue->dev, "Set USB Power On\n");

		if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff)
			axp_gpio_set_value(
				sw_hcd_io->drv_vbus_gpio_set.port_num,
				on_off);
		else
			gpio_write_one_pin_value(
				sw_hcd_io->Drv_vbus_Handle,
				on_off, NULL);

		/* set gpio data */

		/* start session */
		val = musb_readw(musb->mregs, MUSB_DEVCTL);
		val &= ~MUSB_DEVCTL_SESSION;
		musb_writew(musb->mregs, MUSB_DEVCTL, val);
		val |= MUSB_DEVCTL_SESSION;
		musb_writew(musb->mregs, MUSB_DEVCTL, val);

		USBC_ForceVbusValid(musb->mregs, USBC_VBUS_TYPE_HIGH);
	} else {
		if (!sw_hcd_io->vbus_on)
			return; /* already disabled */

		sw_hcd_io->vbus_on = 0;

		dev_info(glue->dev, "Set USB Power Off\n");

		if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff)
			axp_gpio_set_value(
				sw_hcd_io->drv_vbus_gpio_set.port_num, on_off);
		else
			gpio_write_one_pin_value(sw_hcd_io->Drv_vbus_Handle,
					on_off, NULL);

		/* end session */
		val = musb_readw(musb->mregs, MUSB_DEVCTL);
		val &= ~MUSB_DEVCTL_SESSION;
		musb_writew(musb->mregs, MUSB_DEVCTL, val);

		USBC_ForceVbusValid(musb->mregs, USBC_VBUS_TYPE_DISABLE);
	}

	return;
}

/******************************************************************************
 * MUSB Glue code
 ******************************************************************************/

static void sunxi_musb_set_vbus(struct musb *musb, int on)
{
	pr_dbg("%s(): on = %d, otg_state = %s\n", __func__, on,
	       otg_state_string(musb->xceiv->state));

	sw_hcd_board_set_vbus(musb, on);
}

static irqreturn_t sunxi_musb_interrupt(int irq, void *__hci)
{
	struct musb		*musb = __hci;
	irqreturn_t		retval = IRQ_NONE;
	unsigned long		flags;

	spin_lock_irqsave(&musb->lock, flags);

	/* read and flush interrupts */
	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	if (musb->int_usb)
		musb_writeb(musb->mregs, MUSB_INTRUSB, musb->int_usb);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	if (musb->int_tx)
		musb_writew(musb->mregs, MUSB_INTRTX, musb->int_tx);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);
	if (musb->int_rx)
		musb_writew(musb->mregs, MUSB_INTRRX, musb->int_rx);

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval |= musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

static void sunxi_musb_enable(struct musb *musb)
{
	pr_dbg("%s():\n", __func__);

	/* flush pending interrupts */
	musb_writeb(musb->mregs, MUSB_INTRUSB, 0xff);
	musb_writew(musb->mregs, MUSB_INTRTX, 0x3f);
	musb_writew(musb->mregs, MUSB_INTRRX, 0x3f);

	/* select PIO mode */
	USBC_SelectBus(musb->mregs, USBC_IO_TYPE_PIO, 0, 0);

	if (is_host_enabled(musb)) {
		/* port power on */
		sw_hcd_board_set_vbus(musb, 1);
	}
}

static void sunxi_musb_disable(struct musb *musb)
{
	pr_dbg("%s():\n", __func__);
}

static void sunxi_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	/* TODO */
}

static int sunxi_musb_vbus_status(struct musb *musb)
{
	/* TODO? */
	return 0;
}

static int sunxi_musb_set_mode(struct musb *musb, u8 musb_mode)
{
	pr_dbg("%s(): musb_mode %d\n", __func__, musb_mode);

	switch (musb_mode) {
	case MUSB_HOST:
		USBC_ForceId(musb->mregs, USBC_ID_TYPE_HOST);
		USBC_ForceVbusValid(musb->mregs, USBC_VBUS_TYPE_HIGH);
		sw_hcd_board_set_vbus(musb, 1);
		break;

	case MUSB_PERIPHERAL:
		USBC_ForceId(musb->mregs, USBC_ID_TYPE_DEVICE);
		USBC_ForceVbusValid(musb->mregs, USBC_VBUS_TYPE_DISABLE);
		sw_hcd_board_set_vbus(musb, 0);
		break;

	case MUSB_OTG:
	default:
		USBC_ForceId(musb->mregs, USBC_ID_TYPE_DISABLE);
		USBC_ForceVbusValid(musb->mregs, USBC_VBUS_TYPE_DISABLE);
		/* set vbus? */
		break;
	}

	return 0;
}

static int sunxi_musb_init(struct musb *musb)
{
	unsigned long flags = 0;
	struct sunxi_musb_glue *glue = musb_to_glue(musb);
	u32 reg_value = 0;
	int ret = -ENODEV;

	pr_dbg("%s():\n", __func__);

	glue->exiting = 0;

	musb->isr = sunxi_musb_interrupt;

	usb_nop_xceiv_register();
	musb->xceiv = usb_get_transceiver();
	if (!musb->xceiv)
		goto err0;

	ret = usb_clock_init(glue);
	if (ret < 0) {
		dev_err(musb->controller, "failed to get clock\n");
		goto err1;
	}

	ret = open_usb_clock(glue);
	if (ret < 0) {
		dev_err(musb->controller, "failed to enable clocks\n");
		goto err2;
	}

	/* moved here from open_usb_clock */
	UsbPhyInit(musb->mregs, 0);

	/* TODO: Should this be moved elsewhere? */
	/* TODO: Is this needed? */
	/* config usb fifo, 8kb mode */
	spin_lock_irqsave(&musb->lock, flags);
	/* USBC_ConfigFIFO_Base: */
	reg_value = musb_readl((void *)SW_VA_SRAM_IO_BASE, 0x04);
	reg_value &= ~(0x03 << 0);
	reg_value |= (1 << 0);
	musb_writel((void *)SW_VA_SRAM_IO_BASE, 0x04, reg_value);
	spin_unlock_irqrestore(&musb->lock, flags);

	/* config drv_vbus pin */
	ret = pin_init(glue);
	if (ret < 0) {
		dev_err(musb->controller, "pin_init failed\n");
		ret = -ENOMEM;
		goto err3;
	}

	USBC_EnableDpDmPullUp(musb->mregs);
	USBC_EnableIdPullUp(musb->mregs);

	USBC_ForceId(musb->mregs, USBC_ID_TYPE_DISABLE);
	USBC_ForceVbusValid(musb->mregs, USBC_VBUS_TYPE_DISABLE);

	return 0;

/*err4:*/
	pin_exit(glue);
err3:
	close_usb_clock(glue);
err2:
	usb_clock_exit(glue);
err1:
	usb_put_transceiver(musb->xceiv);
err0:
	usb_nop_xceiv_unregister();

	return ret;
}

static int sunxi_musb_exit(struct musb *musb)
{
	struct sunxi_musb_glue *glue = dev_get_drvdata(musb->controller);
	unsigned long flags;

	pr_dbg("%s():\n", __func__);

	spin_lock_irqsave(&musb->lock, flags);
	glue->exiting = 1;
	spin_unlock_irqrestore(&musb->lock, flags);

	USBC_DisableDpDmPullUp(musb->mregs);
	USBC_DisableIdPullUp(musb->mregs);

	sw_hcd_board_set_vbus(musb, 0);

	pin_exit(glue);
	close_usb_clock(glue);
	usb_clock_exit(glue);
	usb_put_transceiver(musb->xceiv);
	usb_nop_xceiv_unregister();

	return 0;
}

static const struct musb_platform_ops sunxi_musb_ops = {
	.init		= sunxi_musb_init,
	.exit		= sunxi_musb_exit,

	.enable		= sunxi_musb_enable,
	.disable	= sunxi_musb_disable,

	.set_vbus	= sunxi_musb_set_vbus,
	.vbus_status	= sunxi_musb_vbus_status,

	.set_mode	= sunxi_musb_set_mode,
	.try_idle	= sunxi_musb_try_idle,
};

static int __devinit sunxi_musb_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct platform_device		*musb;
	struct sunxi_musb_glue		*glue;
	int				ret = -ENOMEM;

	pr_dbg("%s():\n", __func__);

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

	musb = platform_device_alloc("musb-hdrc", -1);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err1;
	}

	glue->dev			= &pdev->dev;
	glue->musb			= musb;

	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= pdev->dev.dma_mask;
	musb->dev.coherent_dma_mask	= pdev->dev.coherent_dma_mask;

	pdata->platform_ops		= &sunxi_musb_ops;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb, pdev->resource,
					    pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err2;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err2;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err2;
	}

	return 0;

err2:
	platform_device_put(musb);

err1:
	kfree(glue);

err0:
	return ret;
}

static int __devexit sunxi_musb_remove(struct platform_device *pdev)
{
	struct sunxi_musb_glue *glue = platform_get_drvdata(pdev);

	platform_device_del(glue->musb);
	platform_device_put(glue->musb);
	kfree(glue);

	return 0;
}

static struct platform_driver sunxi_musb_driver = {
	.probe		= sunxi_musb_probe,
	.remove		= __devexit_p(sunxi_musb_remove),
	.driver		= {
		.name	= SUNXI_MUSB_DRIVER_NAME,
	},
};

MODULE_DESCRIPTION("SUNXI MUSB Glue Layer");
MODULE_AUTHOR("Jussi Kivilinna <jussi.kivilinna@iki.fi>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" SUNXI_MUSB_DRIVER_NAME);

static struct resource sunxi_musb_resources[] = {
	[0] = {
		.start = SW_PA_USB0_IO_BASE,
		.end = SW_PA_USB0_IO_BASE + 0xfff,
		.flags = IORESOURCE_MEM,
		.name = "sunxi_musb0-mem",
	},
	[1] = {
		.start = SW_INT_IRQNO_USB0,
		.end = SW_INT_IRQNO_USB0,
		.flags = IORESOURCE_IRQ,
		.name = "mc", /* hardcoded in musb */
	},
};

/* Can support a maximum ep number, ep0 ~ 5 */
#define USBC_MAX_EP_NUM		6

static struct musb_fifo_cfg sunxi_musb_mode_cfg[] = {
	{ .hw_ep_num =  1, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  1, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  2, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  2, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  3, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  3, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  4, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  4, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  5, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  5, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
};

static struct musb_hdrc_config sunxi_musb_config = {
	.multipoint	= 1,
	.dyn_fifo	= 1,
	.soft_con	= 1,
	.dma		= 0,

	.num_eps	= USBC_MAX_EP_NUM,
	.ram_bits	= 11,

	.fifo_cfg	= sunxi_musb_mode_cfg,
	.fifo_cfg_size	= ARRAY_SIZE(sunxi_musb_mode_cfg),
};

static struct musb_hdrc_platform_data sunxi_musb_plat = {
	.mode		= MUSB_HOST,
	.config		= &sunxi_musb_config,
};

static struct platform_device sunxi_musb_device = {
	.name	= SUNXI_MUSB_DRIVER_NAME,
	.id	= -1,

	.dev = {
		.platform_data = &sunxi_musb_plat,
	},

	.resource = sunxi_musb_resources,
	.num_resources = ARRAY_SIZE(sunxi_musb_resources),
};

static int __init sunxi_musb_drvinit(void)
{
	int ret;

	ret = platform_driver_register(&sunxi_musb_driver);
	if (ret < 0)
		return ret;

	/* TODO: move to arch/arm/... */
	ret = platform_device_register(&sunxi_musb_device);
	if (ret < 0)
		goto err0;

	return 0;
err0:
	platform_driver_unregister(&sunxi_musb_driver);
	return ret;
}
module_init(sunxi_musb_drvinit);

static void __exit sunxi_musb_drvexit(void)
{
	platform_device_unregister(&sunxi_musb_device);
	platform_driver_unregister(&sunxi_musb_driver);
}
module_exit(sunxi_musb_drvexit);