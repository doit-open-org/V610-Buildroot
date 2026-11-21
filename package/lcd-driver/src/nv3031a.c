// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for HXM035002 panels connected to a NewVision NV3031A
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
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#define NV3031A_MADCTL_MV	BIT(5)
#define NV3031A_MADCTL_MX	BIT(6)
#define NV3031A_MADCTL_MY	BIT(7)
#define NV3031A_MAX_PARAMS	8U

#define PANEL_WIDTH  212U
#define PANEL_HEIGHT  318U
/* The physical active-area size is not measured yet. */
#define PANEL_PHYS_W  0U
#define PANEL_PHYS_H  0U

/* The visible area is centered in the 240-column NV3031A GRAM. */
#define HXM035002_LEFT_OFFSET  14U
#define HXM035002_TOP_OFFSET   0U

/* pl022 DMA needs page-aligned TX/RX buffers; alloc_pages guarantees this. */
#define DMA_BUF_BYTES  (PANEL_WIDTH * PANEL_HEIGHT * 2)
#define DMA_BUF_ORDER  get_order(DMA_BUF_BYTES)

/**
 * struct nv3031a_priv - per-device private data
 * @dbidev:      base mipi_dbi_dev (must be first for container_of)
 * @te_gpio:     TE signal GPIO descriptor
 * @te_sem:      semaphore signalled by TE interrupt
 * @te_active:   true after SET_TEAR_ON has been sent to the panel
 * @te_request:  true while nv3031a_command() is blocked waiting for TE;
 *               the ISR only calls up() when this is set, matching the
 *               isRequestFresh pattern in the reference driver
 * @first_frame: true until the very first RAMWR flush completes; the first
 *               flush skips the TE wait (panel not yet scanning)
 * @te_edge_count: TE falling edges seen during the startup validation window
 * @tx_pages:    page-aligned DMA TX buffer (required by pl022)
 * @rx_pages:    page-aligned DMA RX buffer (prevents pl022 dummypage overflow)
 * @rx_buf:      virtual address of rx_pages
 */
struct nv3031a_priv {
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

static inline struct nv3031a_priv *to_nv3031a_priv(struct mipi_dbi_dev *dbidev)
{
	return container_of(dbidev, struct nv3031a_priv, dbidev);
}

/* mipi_dbi_set_window_address() is static in the DRM core. */
static int nv3031a_set_window(struct mipi_dbi_dev *dbidev,
	unsigned int xs, unsigned int xe,
	unsigned int ys, unsigned int ye)
{
	struct mipi_dbi *dbi = &dbidev->dbi;
	int ret;

	if (xs > xe || ys > ye || xe >= PANEL_WIDTH || ye >= PANEL_HEIGHT) {
		return -EINVAL;
	}

	xs += dbidev->left_offset;
	xe += dbidev->left_offset;
	ys += dbidev->top_offset;
	ye += dbidev->top_offset;

	ret = mipi_dbi_command(dbi, MIPI_DCS_SET_COLUMN_ADDRESS, (xs >> 8) & 0xff,
		xs & 0xff, (xe >> 8) & 0xff, xe & 0xff);
	if (ret) {
		return ret;
	}

	return mipi_dbi_command(dbi, MIPI_DCS_SET_PAGE_ADDRESS, (ys >> 8) & 0xff,
		ys & 0xff, (ye >> 8) & 0xff, ye & 0xff);
}

static int nv3031a_spi_transfer(struct nv3031a_priv *priv,
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

	if (!len) {
		return 0;
	}
	if (!buf || !priv->rx_buf) {
		return -EINVAL;
	}
	if (len > DMA_BUF_BYTES) {
		return -EMSGSIZE;
	}
	if (bpw == 16U) {
		if (len & 1U) {
			return -EINVAL;
		}
		max_chunk = ALIGN_DOWN(max_chunk, 2U);
	}
	if (!max_chunk) {
		return -EINVAL;
	}

	/* pl022 needs the device-wide word size, not only transfer->bits_per_word. */
	spi->bits_per_word = bpw;
	ret = spi_setup(spi);
	if (ret) {
		return ret;
	}

	while (len) {
		chunk = min(len, max_chunk);
		tr.tx_buf = buf;
		tr.rx_buf = priv->rx_buf;
		tr.len = chunk;
		buf = (const char *)buf + chunk;
		len -= chunk;

		/* Reinitialize every message to avoid reusing DMA descriptors. */
		spi_message_init_with_transfers(&m, &tr, 1);
		ret = spi_sync(spi, &m);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int nv3031a_command(struct mipi_dbi *dbi, u8 *cmd, u8 *par, size_t num)
{
	struct spi_device *spi = dbi->spi;
	struct mipi_dbi_dev *dbidev = container_of(dbi, struct mipi_dbi_dev, dbi);
	struct nv3031a_priv *priv = to_nv3031a_priv(dbidev);
	u8 bpw = 8;
	u32 speed_hz;
	int ret;

	if (!cmd || (num && !par)) {
		return -EINVAL;
	}

	gpiod_set_value_cansleep(dbi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 1);
	ret = nv3031a_spi_transfer(priv, spi, speed_hz, 8, cmd, 1);
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
	return nv3031a_spi_transfer(priv, spi, speed_hz, bpw, par, num);
}

static irqreturn_t nv3031a_te_isr(int irq, void *dev_id)
{
	struct nv3031a_priv *priv = dev_id;

	atomic_inc(&priv->te_edge_count);
	if (priv->te_request) {
		up(&priv->te_sem);
	}

	return IRQ_HANDLED;
}

static void nv3031a_record_update(struct nv3031a_priv *priv)
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
	struct nv3031a_priv *priv = dev_get_drvdata(dev);
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

static void nv3031a_fb_dirty(struct drm_framebuffer *fb, struct drm_rect *rect)
{
	struct mipi_dbi_dev *dbidev;
	struct mipi_dbi *dbi;
	struct nv3031a_priv *priv;
	unsigned int height;
	unsigned int width;
	int idx;
	int ret = 0;

	if (WARN_ON(!fb || !rect)) {
		return;
	}
	if (rect->x1 < 0 || rect->y1 < 0 || rect->x2 <= rect->x1 ||
		rect->y2 <= rect->y1 || rect->x2 > fb->width || rect->y2 > fb->height) {
		drm_err_once(fb->dev, "Invalid framebuffer damage rectangle\n");
		return;
	}

	width = (unsigned int)(rect->x2 - rect->x1);
	height = (unsigned int)(rect->y2 - rect->y1);
	if (width > PANEL_WIDTH || height > PANEL_HEIGHT) {
		drm_err_once(fb->dev, "Framebuffer damage exceeds panel dimensions\n");
		return;
	}

	dbidev = drm_to_mipi_dbi_dev(fb->dev);
	dbi = &dbidev->dbi;
	priv = to_nv3031a_priv(dbidev);

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
		ret = nv3031a_set_window(dbidev, rect->x1, rect->x2 - 1,
			rect->y1, rect->y2 - 1);
		if (!ret) {
			ret = mipi_dbi_command_buf(dbi, MIPI_DCS_WRITE_MEMORY_START,
				(u8 *)dbidev->tx_buf, width * height * 2U);
		}
	}
	if (!ret)
		nv3031a_record_update(priv);
	if (ret) {
		drm_err_once(fb->dev, "Failed to update display %d\n", ret);
	}

	drm_dev_exit(idx);
}

static void nv3031a_enable_flush(struct mipi_dbi_dev *dbidev,
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

	nv3031a_fb_dirty(fb, &rect);
	if (dbidev->backlight) {
		backlight_enable(dbidev->backlight);
	}

	drm_dev_exit(idx);
}

struct nv3031a_init_command {
	u8 command;
	u8 data_len;
	u8 data[NV3031A_MAX_PARAMS];
};

/*
 * This table is the register/data order from the supplied HXM035002 NV3031A
 * initialization file. EXIT_SLEEP_MODE and SET_DISPLAY_ON are sent below so
 * their required delays remain explicit.
 */
static const struct nv3031a_init_command nv3031a_vendor_init[] = {
	/* Private access and analog settings. */
	{ .command = 0xfd, .data_len = 2, .data = { 0x06, 0x08 } },
	{ .command = 0x61, .data_len = 2, .data = { 0x07, 0x07 } },
	{ .command = 0x73, .data_len = 1, .data = { 0x70 } },
	{ .command = 0x73, .data_len = 1, .data = { 0x00 } },
	{ .command = 0x62, .data_len = 3, .data = { 0x00, 0x44, 0x40 } },
	{ .command = 0x63, .data_len = 4, .data = { 0x41, 0x03, 0x12, 0x12 } },
	{ .command = 0x64, .data_len = 3, .data = { 0x23, 0x04, 0x03 } },
	{ .command = 0x65, .data_len = 3, .data = { 0x09, 0x17, 0x21 } },
	{ .command = 0x66, .data_len = 3, .data = { 0x09, 0x17, 0x21 } },
	{ .command = 0x67, .data_len = 2, .data = { 0x21, 0x40 } },
	{ .command = 0x68, .data_len = 4, .data = { 0x80, 0x30, 0x68, 0x50 } },
	{ .command = 0xb1, .data_len = 3, .data = { 0x0f, 0x02, 0x02 } },
	{ .command = 0xb4, .data_len = 1, .data = { 0x01 } },
	{ .command = 0xb5, .data_len = 4, .data = { 0x02, 0x02, 0x0a, 0x14 } },
	{ .command = 0xb6, .data_len = 5, .data = { 0x04, 0x01, 0x9f, 0x00, 0x02 } },
	{ .command = 0xdf, .data_len = 1, .data = { 0x11 } },
	{ .command = 0xe6, .data_len = 2, .data = { 0x00, 0xff } },
	{ .command = 0xe7, .data_len = 6, .data = { 0x01, 0x04, 0x03, 0x03, 0x00, 0x12 } },
	{ .command = 0xec, .data_len = 1, .data = { 0x54 } },
	{ .command = 0xe8, .data_len = 3, .data = { 0x00, 0x70, 0x00 } },

	/* Gamma 2.2. */
	{ .command = 0xe0, .data_len = 8, .data = { 0x06, 0x01, 0x0f, 0x16,
		0x08, 0x0e, 0x06, 0x0f } },
	{ .command = 0xe3, .data_len = 8, .data = { 0x38, 0x18, 0x15, 0x19,
		0x16, 0x0f, 0x08, 0x06 } },
	{ .command = 0xe1, .data_len = 2, .data = { 0x02, 0x2c } },
	{ .command = 0xe4, .data_len = 2, .data = { 0x50, 0x02 } },
	{ .command = 0xe2, .data_len = 6, .data = { 0x06, 0x01, 0x00, 0x11,
		0x13, 0x3a } },
	{ .command = 0xe5, .data_len = 6, .data = { 0x3c, 0x13, 0x12, 0x01,
		0x01, 0x06 } },

	/* TE, SPI interface, private lock, and standard DCS setup. */
	{ .command = 0xf1, .data_len = 3, .data = { 0x01, 0x01, 0x20 } },
	{ .command = 0xf6, .data_len = 4, .data = { 0x01, 0x30, 0x00, 0x00 } },
	{ .command = 0xfd, .data_len = 2, .data = { 0xfa, 0xfc } },
	{ .command = MIPI_DCS_SET_PIXEL_FORMAT, .data_len = 1, .data = { 0x55 } },
	{ .command = MIPI_DCS_SET_TEAR_ON, .data_len = 1, .data = { 0x00 } },
	{ .command = MIPI_DCS_SET_ADDRESS_MODE, .data_len = 1, .data = { 0x00 } },
	{ .command = MIPI_DCS_ENTER_INVERT_MODE, .data_len = 0 },
};

static int nv3031a_write_vendor_init(struct mipi_dbi *dbi)
{
	size_t index;
	int ret;

	for (index = 0; index < ARRAY_SIZE(nv3031a_vendor_init); index++) {
		const struct nv3031a_init_command *entry = &nv3031a_vendor_init[index];

		if (entry->data_len > ARRAY_SIZE(entry->data)) {
			return -EINVAL;
		}
		if (entry->data_len) {
			ret = mipi_dbi_command_stackbuf(dbi, entry->command, entry->data,
				entry->data_len);
		} else {
			ret = mipi_dbi_command(dbi, entry->command);
		}
		if (ret) {
			dev_err(&dbi->spi->dev,
				"NV3031A init command 0x%02x at index %zu failed: %d\n",
				entry->command, index, ret);
			return ret;
		}
	}

	return 0;
}

static int nv3031a_set_address_mode(struct mipi_dbi_dev *dbidev)
{
	u8 addr_mode;

	switch (dbidev->rotation) {
	case 90:
		addr_mode = NV3031A_MADCTL_MX | NV3031A_MADCTL_MV;
		break;
	case 180:
		addr_mode = NV3031A_MADCTL_MX | NV3031A_MADCTL_MY;
		break;
	case 270:
		addr_mode = NV3031A_MADCTL_MV | NV3031A_MADCTL_MY;
		break;
	default:
		addr_mode = 0x00;
		break;
	}

	return mipi_dbi_command(&dbidev->dbi, MIPI_DCS_SET_ADDRESS_MODE,
		addr_mode);
}

static void nv3031a_pipe_enable(struct drm_simple_display_pipe *pipe,
	struct drm_crtc_state *crtc_state,
	struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct nv3031a_priv *priv = to_nv3031a_priv(dbidev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	int ret;
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx)) {
		return;
	}

	priv->te_active = false;
	priv->te_request = false;
	priv->first_frame = true;

	ret = mipi_dbi_poweron_reset(dbidev);
	if (ret) {
		dev_err(&dbi->spi->dev, "NV3031A reset failed: %d\n", ret);
		goto out_exit;
	}

	ret = nv3031a_write_vendor_init(dbi);
	if (ret) {
		goto out_exit;
	}

	ret = mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	if (ret) {
		dev_err(&dbi->spi->dev, "NV3031A sleep exit failed: %d\n", ret);
		goto out_exit;
	}
	msleep(200);

	ret = mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	if (ret) {
		dev_err(&dbi->spi->dev, "NV3031A display-on failed: %d\n", ret);
		goto out_exit;
	}
	msleep(20);

	/* Count only during SPI idle time to avoid treating crosstalk as TE. */
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

	ret = nv3031a_set_address_mode(dbidev);
	if (ret) {
		dev_err(&dbi->spi->dev, "NV3031A address mode failed: %d\n", ret);
		goto out_exit;
	}
	nv3031a_enable_flush(dbidev, plane_state);

out_exit:
	drm_dev_exit(idx);
}

static void nv3031a_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct nv3031a_priv *priv = to_nv3031a_priv(dbidev);
	struct mipi_dbi     *dbi  = &dbidev->dbi;
	int ret;

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
	ret = mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
	if (ret) {
		dev_warn(&dbi->spi->dev, "NV3031A display-off failed: %d\n", ret);
	}
	msleep(20);
	ret = mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE);
	if (ret) {
		dev_warn(&dbi->spi->dev, "NV3031A sleep enter failed: %d\n", ret);
	}
	msleep(120);   /* SLPIN requires a wait before the next SLPOUT */

	mipi_dbi_pipe_disable(pipe);
}

static void nv3031a_pipe_update(struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_rect rect;

	if (!pipe->crtc.state->active) {
		return;
	}

	if (drm_atomic_helper_damage_merged(old_state, state, &rect)) {
		nv3031a_fb_dirty(state->fb, &rect);
	}
}

static const struct drm_simple_display_pipe_funcs nv3031a_pipe_funcs = {
	.enable = nv3031a_pipe_enable,
	.disable = nv3031a_pipe_disable,
	.update = nv3031a_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode nv3031a_mode = {
	DRM_SIMPLE_MODE(PANEL_WIDTH, PANEL_HEIGHT, PANEL_PHYS_W, PANEL_PHYS_H),
};

DEFINE_DRM_GEM_CMA_FOPS(nv3031a_fops);

static struct drm_driver nv3031a_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &nv3031a_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init = mipi_dbi_debugfs_init,
	.name = "nv3031a",
	.desc = "HXM035002 NV3031A 212x318 DRM SPI panel driver",
	.date = "20260921",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id nv3031a_of_match[] = {
	{ .compatible = "hxm,hxm035002-nv3031a" },
	{ },
};
MODULE_DEVICE_TABLE(of, nv3031a_of_match);

static const struct spi_device_id nv3031a_id[] = {
	{ "nv3031a", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, nv3031a_id);

static int nv3031a_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct nv3031a_priv *priv;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;
	int te_irq;

	priv = devm_drm_dev_alloc(dev, &nv3031a_driver,
		struct nv3031a_priv, dbidev.drm);
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
	/* The verified GRAM offset/window geometry is for the native portrait mode. */
	if (rotation != 0) {
		dev_err(dev, "use rotation=0; rotate the 212x318 image in the application\n");
		return -EINVAL;
	}

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
			nv3031a_te_isr,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"hxm035002-nv3031a-te", priv);
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

	dbi->command = nv3031a_command;

	ret = mipi_dbi_dev_init(dbidev, &nv3031a_pipe_funcs,
		&nv3031a_mode, rotation);
	if (ret) {
		return ret;
	}
	/* Keep logical coordinates aligned to the measured visible panel area. */
	dbidev->left_offset = HXM035002_LEFT_OFFSET;
	dbidev->top_offset = HXM035002_TOP_OFFSET;

	/*
	 * Both buffers must be page-aligned. pl022 derives scatter-gather entries
	 * from DIV_ROUND_UP(len, PAGE_SIZE), so an unaligned buffer can cross one
	 * extra page and trigger BUG_ON(bytesleft != 0).
	 */
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

	dev_info(dev, "HXM035002 NV3031A %ux%u probed (rotation=%u, TE %s)\n",
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

static int nv3031a_remove(struct spi_device *spi)
{
	struct nv3031a_priv *priv = spi_get_drvdata(spi);

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

static void nv3031a_shutdown(struct spi_device *spi)
{
	struct nv3031a_priv *priv = spi_get_drvdata(spi);

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

static struct spi_driver nv3031a_spi_driver = {
	.driver = {
		.name = "nv3031a",
		.of_match_table = nv3031a_of_match,
	},
	.id_table = nv3031a_id,
	.probe = nv3031a_probe,
	.remove = nv3031a_remove,
	.shutdown = nv3031a_shutdown,
};

module_spi_driver(nv3031a_spi_driver);

MODULE_DESCRIPTION("HXM035002 NV3031A 212x318 DRM SPI panel driver");
MODULE_LICENSE("GPL");
