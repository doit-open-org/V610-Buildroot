// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for display panels connected to a Sitronix ST7789V
 * display controller in SPI mode, with TE (tearing effect) sync.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>
#include <linux/backlight.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

/* ST7789V command set */
#define ST7789_NOP			0x00
#define ST7789_SWRESET		0x01
#define ST7789_RDDID		0x04
#define ST7789_RDDST		0x09
#define ST7789_RDDPM		0x0A
#define ST7789_RDD_MADCTL	0x0B
#define ST7789_RDD_COLMOD	0x0C
#define ST7789_RDDIM		0x0D
#define ST7789_RDDSM		0x0E
#define ST7789_RDDSR		0x0F
#define ST7789_SLPIN		0x10
#define ST7789_SLPOUT		0x11
#define ST7789_PTLON		0x12
#define ST7789_NORON		0x13
#define ST7789_INVOFF		0x20
#define ST7789_INVON		0x21
#define ST7789_GAMSET		0x26
#define ST7789_DISPOFF		0x28
#define ST7789_DISPON		0x29
#define ST7789_CASET		0x2A
#define ST7789_RASET		0x2B
#define ST7789_RAMWR		0x2C
#define ST7789_RGBSET		0x2D
#define ST7789_RAMRD		0x2E
#define ST7789_PTLAR		0x30
#define ST7789_VSCRDEF		0x33
#define ST7789_TEOFF		0x34
#define ST7789_TEON			0x35
#define ST7789_MADCTL		0x36
#define ST7789_IDMOFF		0x38
#define ST7789_IDMON		0x39
#define ST7789_RAMWRC		0x3C
#define ST7789_RAMRDC		0x3E
#define ST7789_COLMOD		0x3A
#define ST7789_RAMCTRL		0xB0
#define ST7789_RGBCTRL		0xB1
#define ST7789_PORCTRL		0xB2
#define ST7789_FRCTRL1		0xB3
#define ST7789_PARCTRL		0xB5
#define ST7789_GCTRL		0xB7
#define ST7789_GTADJ		0xB8
#define ST7789_DGMEN		0xBA
#define ST7789_VCOMS		0xBB
#define ST7789_LCMCTRL		0xC0
#define ST7789_IDSET		0xC1
#define ST7789_VDVVRHEN		0xC2
#define ST7789_VRHS			0xC3
#define ST7789_VDVSET		0xC4
#define ST7789_VCMOFSET		0xC5
#define ST7789_FRCTR2		0xC6
#define ST7789_CABCCTRL		0xC7
#define ST7789_REGSEL1		0xC8
#define ST7789_REGSEL2		0xCA
#define ST7789_PWMFRSEL		0xCC
#define ST7789_PWCTRL1		0xD0
#define ST7789_VAPVANEN		0xD2
#define ST7789_CMD2EN		0xDF
#define ST7789_PVGAMCTRL	0xE0
#define ST7789_NVGAMCTRL	0xE1
#define ST7789_DGMLUTR		0xE2
#define ST7789_DGMLUTB		0xE3
#define ST7789_GATECTRL		0xE4
#define ST7789_SPI2EN		0xE7
#define ST7789_PWCTRL2		0xD6
#define ST7789_EQCTRL		0xE9
#define ST7789_PROMCTRL		0xEC
#define ST7789_PROMEN		0xFA
#define ST7789_NVMSET		0xFC
#define ST7789_PROMACT		0xFE

#define ST7789_MADCTL_BGR	BIT(3)
#define ST7789_MADCTL_MV	BIT(5)
#define ST7789_MADCTL_MX	BIT(6)
#define ST7789_MADCTL_MY	BIT(7)

#define PANEL_WIDTH   240
#define PANEL_HEIGHT  320
#define PANEL_PHYS_W  26
#define PANEL_PHYS_H  30

/* pl022 DMA 要求 TX/RX 缓冲区页对齐，用 alloc_pages 保证对齐 */
#define DMA_BUF_BYTES  (PANEL_WIDTH * PANEL_HEIGHT * 2)
#define DMA_BUF_ORDER  get_order(DMA_BUF_BYTES)

/**
 * struct st7789v_priv - per-device private data
 * @dbidev:      base mipi_dbi_dev (must be first for container_of)
 * @te_gpio:     TE signal GPIO descriptor
 * @te_sem:      semaphore signalled by TE interrupt
 * @te_active:   true after SET_TEAR_ON has been sent to the panel
 * @te_request:  true while st7789v_command() is blocked waiting for TE;
 *               the ISR only calls up() when this is set, matching the
 *               isRequestFresh pattern in the reference driver
 * @first_frame: true until the very first RAMWR flush completes; the first
 *               flush skips the TE wait (panel not yet scanning)
 * @te_edge_count: TE falling edges seen during the startup validation window
 * @tx_pages:    page-aligned DMA TX buffer (required by pl022)
 * @rx_pages:    page-aligned DMA RX buffer (prevents pl022 dummypage overflow)
 * @rx_buf:      virtual address of rx_pages
 */
struct st7789v_priv {
	struct mipi_dbi_dev dbidev;
	struct gpio_desc *te_gpio;
	struct semaphore te_sem;
	bool te_active;
	bool te_request;
	bool first_frame;
	atomic_t te_edge_count;
	struct page *tx_pages;
	struct page *rx_pages;
	void *rx_buf;
	spinlock_t stats_lock;
	u64 stats_window_start_ns;
	u64 stats_window_updates;
	u32 stats_fps_x100;
};

static inline struct st7789v_priv *to_st7789v_priv(struct mipi_dbi_dev *dbidev)
{
	return container_of(dbidev, struct st7789v_priv, dbidev);
}

/*
 * mipi_dbi_set_window_address is static in the core, duplicate here.
 */
static void st7789v_set_window(struct mipi_dbi_dev *dbidev,
	unsigned int xs, unsigned int xe,
	unsigned int ys, unsigned int ye)
{
	struct mipi_dbi *dbi = &dbidev->dbi;

	xs += dbidev->left_offset;
	xe += dbidev->left_offset;
	ys += dbidev->top_offset;
	ye += dbidev->top_offset;

	mipi_dbi_command(dbi, MIPI_DCS_SET_COLUMN_ADDRESS, (xs >> 8) & 0xff,
		xs & 0xff, (xe >> 8) & 0xff, xe & 0xff);
	mipi_dbi_command(dbi, MIPI_DCS_SET_PAGE_ADDRESS, (ys >> 8) & 0xff,
		ys & 0xff, (ye >> 8) & 0xff, ye & 0xff);
}

static int st7789v_spi_transfer(struct st7789v_priv *priv,
	struct spi_device *spi, u32 speed_hz,
	u8 bpw, const void *buf, size_t len)
{
	size_t max_chunk = spi_max_transfer_size(spi);
	struct spi_transfer tr = {
		.bits_per_word = bpw,
		.speed_hz      = speed_hz,
	};
	struct spi_message m;
	size_t chunk;
	int ret;

	max_chunk = ALIGN_DOWN(max_chunk, 2);

	/* pl022 需在 device 级更新 bpw，仅改 transfer 级无效 */
	spi->bits_per_word = bpw;
	spi_setup(spi);

	while (len) {
		chunk = min(len, max_chunk);
		tr.tx_buf = buf;
		tr.rx_buf = priv->rx_buf;
		tr.len = chunk;
		buf = (const char *)buf + chunk;
		len -= chunk;

		/* 每次 spi_sync 前重新初始化，避免 DMA 描述符复用 */
		spi_message_init_with_transfers(&m, &tr, 1);
		ret = spi_sync(spi, &m);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int st7789v_command(struct mipi_dbi *dbi, u8 *cmd, u8 *par, size_t num)
{
	struct spi_device *spi = dbi->spi;
	struct mipi_dbi_dev *dbidev = container_of(dbi, struct mipi_dbi_dev, dbi);
	struct st7789v_priv *priv = to_st7789v_priv(dbidev);
	unsigned int bpw = 8;
	u32 speed_hz;
	int ret;

	gpiod_set_value_cansleep(dbi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 1);
	ret = st7789v_spi_transfer(priv, spi, speed_hz, 8, cmd, 1);
	if (ret || !num) {
		return ret;
	}

	if (*cmd == MIPI_DCS_WRITE_MEMORY_START) {
		bpw = 16;
		if (priv->te_active && !priv->first_frame) {
			/*
			 * Wait for the TE sync signal. Normally the TE IRQ
			 * wakes us up immediately; if TE stops (e.g. during
			 * exit or panel reset), give up sync after 100ms and
			 * continue, to avoid hanging in D state forever.
			 */
			sema_init(&priv->te_sem, 0);
			priv->te_request = true;
			if (down_timeout(&priv->te_sem, msecs_to_jiffies(100)) != 0) {
				dev_warn_ratelimited(&spi->dev, "TE wait timeout, skip sync\n");
			}
			priv->te_request = false;
		}
		priv->first_frame = false;
	}

	gpiod_set_value_cansleep(dbi->dc, 1);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, num);
	if (*cmd == MIPI_DCS_WRITE_MEMORY_START)
		return st7789v_spi_transfer(priv, spi, speed_hz, bpw, par, num);

	return st7789v_spi_transfer(priv, spi, speed_hz, bpw, par, num);
}

static irqreturn_t st7789v_te_isr(int irq, void *dev_id)
{
	struct st7789v_priv *priv = dev_id;

	atomic_inc(&priv->te_edge_count);
	if (priv->te_request) {
		up(&priv->te_sem);
	}

	return IRQ_HANDLED;
}

static void st7789v_record_update(struct st7789v_priv *priv)
{
	u64 now_ns = ktime_get_ns();
	u64 elapsed_ns;
	unsigned long flags;

	spin_lock_irqsave(&priv->stats_lock, flags);
	priv->stats_window_updates++;
	elapsed_ns = now_ns - priv->stats_window_start_ns;
	if (elapsed_ns >= NSEC_PER_SEC) {
		priv->stats_fps_x100 = div64_u64(
			priv->stats_window_updates * 100 * NSEC_PER_SEC,
			elapsed_ns);
		priv->stats_window_start_ns = now_ns;
		priv->stats_window_updates = 0;
	}
	spin_unlock_irqrestore(&priv->stats_lock, flags);
}

static ssize_t display_stats_show(struct device *dev,
	struct device_attribute *attribute, char *buf)
{
	struct st7789v_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;
	u32 fps_x100;

	(void)attribute;
	if (!priv)
		return -ENODEV;

	spin_lock_irqsave(&priv->stats_lock, flags);
	fps_x100 = priv->stats_fps_x100;
	spin_unlock_irqrestore(&priv->stats_lock, flags);

	return sysfs_emit(buf, "fps=%u.%02u\n",
		fps_x100 / 100, fps_x100 % 100);
}
static DEVICE_ATTR_RO(display_stats);

static void st7789v_fb_dirty(struct drm_framebuffer *fb, struct drm_rect *rect)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(fb->dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	struct st7789v_priv *priv = to_st7789v_priv(dbidev);
	unsigned int height = rect->y2 - rect->y1;
	unsigned int width = rect->x2 - rect->x1;
	int idx;
	int ret = 0;

	if (WARN_ON(!fb)) {
		return;
	}

	if (!drm_dev_enter(fb->dev, &idx)) {
		return;
	}
	/*
	 * Always copy through the page-aligned tx_buf; direct use of
	 * cma_obj->vaddr risks DMA coherency issues (CPU cache not flushed).
	 * swap=false: bpw=16 sends u16 MSB-first by hardware, no software swap.
	 */
	ret = mipi_dbi_buf_copy(dbidev->tx_buf, fb, rect, false);
	if (!ret) {
		st7789v_set_window(dbidev, rect->x1, rect->x2 - 1,
			rect->y1, rect->y2 - 1);
		ret = mipi_dbi_command_buf(dbi, MIPI_DCS_WRITE_MEMORY_START,
			(u8 *)dbidev->tx_buf,
			width * height * 2);
	}
	if (!ret)
		st7789v_record_update(priv);
	if (ret) {
		drm_err_once(fb->dev, "Failed to update display %d\n", ret);
	}

	drm_dev_exit(idx);
}

static void st7789v_enable_flush(struct mipi_dbi_dev *dbidev,
	struct drm_plane_state *plane_state)
{
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_rect rect = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};
	int idx;

	if (!drm_dev_enter(&dbidev->drm, &idx)) {
		return;
	}

	st7789v_fb_dirty(fb, &rect);
	if (dbidev->backlight) {
		backlight_enable(dbidev->backlight);
	}

	drm_dev_exit(idx);
}

static void st7789v_init(struct mipi_dbi *dbi)
{
	mipi_dbi_command(dbi, ST7789_MADCTL, 0x00);
	mipi_dbi_command(dbi, ST7789_COLMOD, 0x05);
	mipi_dbi_command(dbi, ST7789_RAMCTRL, 0x00, 0xe0);

	mipi_dbi_command(dbi, ST7789_PORCTRL, 0x0c, 0x0c, 0x00, 0x33, 0x33);
	mipi_dbi_command(dbi, ST7789_GCTRL, 0x02);
	mipi_dbi_command(dbi, ST7789_VCOMS, 0x19);
	mipi_dbi_command(dbi, ST7789_LCMCTRL, 0x2c);
	mipi_dbi_command(dbi, ST7789_VDVVRHEN, 0x01);
	mipi_dbi_command(dbi, ST7789_VRHS, 0x13);
	mipi_dbi_command(dbi, ST7789_FRCTR2, 0x0f);
	mipi_dbi_command(dbi, ST7789_PWCTRL1, 0xa7);
	mipi_dbi_command(dbi, ST7789_PWCTRL1, 0xa4, 0xa1);
	mipi_dbi_command(dbi, ST7789_PWCTRL2, 0xa1);

	mipi_dbi_command(dbi, ST7789_PVGAMCTRL,
		0xf0, 0x0f, 0x16, 0x07, 0x08, 0x25, 0x36,
		0x54, 0x4b, 0x37, 0x13, 0x13, 0x2e, 0x35);
	mipi_dbi_command(dbi, ST7789_NVGAMCTRL,
		0xf0, 0x10, 0x14, 0x08, 0x06, 0x23, 0x35,
		0x44, 0x4a, 0x38, 0x10, 0x10, 0x2c, 0x33);

	mipi_dbi_command(dbi, ST7789_INVON);
	mipi_dbi_command(dbi, ST7789_CASET, 0x00, 0x00, 0x00, 0xef);
	mipi_dbi_command(dbi, ST7789_RASET, 0x00, 0x00, 0x01, 0x3f);
}

static void st7789v_pipe_enable(struct drm_simple_display_pipe *pipe,
	struct drm_crtc_state *crtc_state,
	struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct st7789v_priv *priv = to_st7789v_priv(dbidev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 addr_mode;
	int ret;
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx)) {
		return;
	}

	ret = mipi_dbi_poweron_conditional_reset(dbidev);
	if (ret < 0) {
		goto out_exit;
	}
	if (ret == 1) {
		goto out_enable;
	}

	mipi_dbi_command(dbi, ST7789_SLPOUT);
	msleep(120);
	st7789v_init(dbi);

	if (priv->te_gpio) {
		mipi_dbi_command(dbi, MIPI_DCS_SET_TEAR_ON, 0x00);
		mipi_dbi_command(dbi, MIPI_DCS_SET_TEAR_SCANLINE, 0x00, 0x00);
	}

	mipi_dbi_command(dbi, ST7789_DISPON);
	/* Count edges only while SPI is idle, so a floating TE line cannot mistake
	 * SPI crosstalk for a valid panel sync waveform. */
	atomic_set(&priv->te_edge_count, 0);
	msleep(120);
	if (priv->te_gpio) {
		int edge_count = atomic_read(&priv->te_edge_count);

		if (edge_count > 0) {
			priv->te_active = true;
		} else {
			priv->te_active = false;
			dev_warn(&dbi->spi->dev,
				"TE signal not detected, sync disabled\n");
		}
	}

out_enable:
	switch (dbidev->rotation) {
	case 90:
		addr_mode = ST7789_MADCTL_MX | ST7789_MADCTL_MV;
		break;
	case 180:
		addr_mode = ST7789_MADCTL_MX | ST7789_MADCTL_MY;
		break;
	case 270:
		addr_mode = ST7789_MADCTL_MV | ST7789_MADCTL_MY;
		break;
	default:
		addr_mode = 0x00;
		break;
	}

	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	st7789v_enable_flush(dbidev, plane_state);

out_exit:
	drm_dev_exit(idx);
}

static void st7789v_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct st7789v_priv *priv = to_st7789v_priv(dbidev);
	struct mipi_dbi     *dbi  = &dbidev->dbi;

	priv->te_active = false;
	priv->te_request = false;
	priv->first_frame = true;
	if (dbidev->backlight) {
		backlight_disable(dbidev->backlight);
	}

	/*
	 * Force the panel to sleep state so each run ends deterministically.
	 * mipi_dbi_pipe_disable() only turns off the backlight; without
	 * DISPLAY_OFF + SLPIN the panel keeps scanning, and the next
	 * enable's reset+init runs on an undefined state, causing
	 * alternating black-screen issues across runs.
	 */
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(20);
	mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);   /* SLPIN requires a wait before the next SLPOUT */

	mipi_dbi_pipe_disable(pipe);
}

static void st7789v_pipe_update(struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_rect rect;

	if (!pipe->crtc.state->active) {
		return;
	}

	if (drm_atomic_helper_damage_merged(old_state, state, &rect)) {
		st7789v_fb_dirty(state->fb, &rect);
	}
}

static const struct drm_simple_display_pipe_funcs st7789v_pipe_funcs = {
	.enable = st7789v_pipe_enable,
	.disable = st7789v_pipe_disable,
	.update = st7789v_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode st7789v_mode = {
	DRM_SIMPLE_MODE(PANEL_WIDTH, PANEL_HEIGHT, PANEL_PHYS_W, PANEL_PHYS_H),
};

DEFINE_DRM_GEM_CMA_FOPS(st7789v_fops);

static struct drm_driver st7789v_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &st7789v_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init = mipi_dbi_debugfs_init,
	.name = "st7789v",
	.desc = "Sitronix ST7789V 240x320",
	.date = "20211022",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id st7789v_of_match[] = {
	{ .compatible = "sitronix,st7789v" },
	{ },
};
MODULE_DEVICE_TABLE(of, st7789v_of_match);

static const struct spi_device_id st7789v_id[] = {
	{ "st7789v", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, st7789v_id);

static int st7789v_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct st7789v_priv *priv;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;
	int te_irq;

	priv = devm_drm_dev_alloc(dev, &st7789v_driver,
		struct st7789v_priv, dbidev.drm);
	if (IS_ERR(priv)) {
		return PTR_ERR(priv);
	}

	dbidev = &priv->dbidev;
	dbi = &dbidev->dbi;
	drm = &dbidev->drm;
	dbi->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset)) {
		dev_err(dev, "Failed to get reset GPIO\n");
		return PTR_ERR(dbi->reset);
	}

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		dev_err(dev, "Failed to get dc GPIO\n");
		return PTR_ERR(dc);
	}

	device_property_read_u32(dev, "rotation", &rotation);

	/* TE signal: get GPIO from DTS "te-gpios", configure as input */
	sema_init(&priv->te_sem, 0);
	atomic_set(&priv->te_edge_count, 0);
	spin_lock_init(&priv->stats_lock);
	priv->stats_window_start_ns = ktime_get_ns();
	priv->te_active   = false;
	priv->te_request  = false;
	priv->first_frame = true;

	priv->te_gpio = devm_gpiod_get_optional(dev, "te", GPIOD_IN);
	if (IS_ERR(priv->te_gpio)) {
		dev_err(dev, "Failed to get te GPIO\n");
		return PTR_ERR(priv->te_gpio);
	}

	if (priv->te_gpio) {
		te_irq = gpiod_to_irq(priv->te_gpio);
		if (te_irq < 0) {
			dev_err(dev, "Failed to get TE IRQ: %d\n", te_irq);
			return te_irq;
		}

		ret = devm_request_threaded_irq(dev, te_irq, NULL,
			st7789v_te_isr,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"st7789v-te", priv);
		if (ret) {
			dev_err(dev, "Failed to request TE IRQ: %d\n", ret);
			return ret;
		}
	}

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight)) {
		dev_err(dev, "Failed to get backlight: %ld\n",
			PTR_ERR(dbidev->backlight));
		return PTR_ERR(dbidev->backlight);
	}
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(dev, "Failed to configure SPI mode 3: %d\n", ret);
		return ret;
	}

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret) {
		return ret;
	}

	dbi->command = st7789v_command;

	ret = mipi_dbi_dev_init(dbidev, &st7789v_pipe_funcs,
		&st7789v_mode, rotation);
	if (ret) {
		return ret;
	}

	/* TX/RX 均需页对齐：pl022 setup_dma_scatter 按 DIV_ROUND_UP(len, PAGE_SIZE)
	 * 分配 SG 条目，非页对齐缓冲区会跨多一页导致 BUG_ON(bytesleft!=0) */
	priv->tx_pages = alloc_pages(GFP_KERNEL, DMA_BUF_ORDER);
	if (!priv->tx_pages) {
		dev_err(dev, "failed to alloc TX DMA buffer (order %u, %zu bytes)\n",
			DMA_BUF_ORDER, DMA_BUF_BYTES);
		return -ENOMEM;
	}
	dbidev->tx_buf = page_address(priv->tx_pages);

	priv->rx_pages = alloc_pages(GFP_KERNEL, DMA_BUF_ORDER);
	if (!priv->rx_pages) {
		dev_err(dev, "failed to alloc RX DMA buffer (order %u, %zu bytes)\n",
			DMA_BUF_ORDER, DMA_BUF_BYTES);
		__free_pages(priv->tx_pages, DMA_BUF_ORDER);
		priv->tx_pages = NULL;
		return -ENOMEM;
	}
	priv->rx_buf = page_address(priv->rx_pages);

	drm_mode_config_reset(drm);

	/* Set drvdata before drm_dev_register so remove() can always find priv
	 * if the device is hot-unplugged immediately after registration. */
	spi_set_drvdata(spi, priv);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		goto err_free_pages;
	}

	drm_fbdev_generic_setup(drm, 0);
	ret = device_create_file(dev, &dev_attr_display_stats);
	if (ret) {
		dev_warn(dev, "failed to create display_stats: %d\n", ret);
	}

	dev_info(dev, "Sitronix ST7789V %dx%d probed (rotation=%u, TE %s)\n",
		PANEL_WIDTH, PANEL_HEIGHT, rotation,
		priv->te_gpio ? "wired" : "not wired");
	return 0;

err_free_pages:
	__free_pages(priv->rx_pages, DMA_BUF_ORDER);
	__free_pages(priv->tx_pages, DMA_BUF_ORDER);
	priv->rx_pages      = NULL;
	priv->tx_pages      = NULL;
	return ret;
}

static int st7789v_remove(struct spi_device *spi)
{
	struct st7789v_priv *priv = spi_get_drvdata(spi);

	device_remove_file(&spi->dev, &dev_attr_display_stats);
	drm_dev_unplug(&priv->dbidev.drm);
	drm_atomic_helper_shutdown(&priv->dbidev.drm);

	if (priv->rx_pages) {
		__free_pages(priv->rx_pages, DMA_BUF_ORDER);
		priv->rx_pages        = NULL;
		priv->rx_buf          = NULL;
	}
	if (priv->tx_pages) {
		__free_pages(priv->tx_pages, DMA_BUF_ORDER);
		priv->tx_pages        = NULL;
		priv->dbidev.tx_buf   = NULL;
	}
	return 0;
}

static void st7789v_shutdown(struct spi_device *spi)
{
	struct st7789v_priv *priv = spi_get_drvdata(spi);

	drm_atomic_helper_shutdown(&priv->dbidev.drm);

	if (priv->rx_pages) {
		__free_pages(priv->rx_pages, DMA_BUF_ORDER);
		priv->rx_pages      = NULL;
		priv->rx_buf        = NULL;
	}
	if (priv->tx_pages) {
		__free_pages(priv->tx_pages, DMA_BUF_ORDER);
		priv->tx_pages      = NULL;
		priv->dbidev.tx_buf = NULL;
	}
}

static struct spi_driver st7789v_spi_driver = {
	.driver = {
		.name = "st7789v",
		.of_match_table = st7789v_of_match,
	},
	.id_table = st7789v_id,
	.probe = st7789v_probe,
	.remove = st7789v_remove,
	.shutdown = st7789v_shutdown,
};

module_spi_driver(st7789v_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7789V 240x320 DRM SPI panel driver");
MODULE_LICENSE("GPL");
