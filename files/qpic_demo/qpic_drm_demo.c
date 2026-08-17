/*
 * qpic_drm_demo.c — U60Pro (MU5250) DRM/QPIC screen demo
 *
 * Atomic KMS + dumb RGB565 buffers (matches zte_topsw_devui path).
 * Scenes: RGB → mosaic → clock → touch pointer-location → blank.
 * Build: see build.sh / docs/screen.md
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include(<libdrm/drm.h>)
#    include <libdrm/drm.h>
#    include <libdrm/drm_mode.h>
#  elif __has_include(<drm/drm.h>)
#    include <drm/drm.h>
#    include <drm/drm_mode.h>
#  else
#    include <drm/drm.h>
#    include <drm/drm_mode.h>
#  endif
#else
#  include <drm/drm.h>
#  include <drm/drm_mode.h>
#endif

#ifndef DRM_FORMAT_RGB565
#ifndef fourcc_code
#define fourcc_code(a, b, c, d) \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#endif
#define DRM_FORMAT_RGB565 fourcc_code('R', 'G', '1', '6')
#endif

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif

#ifndef DRM_CLIENT_CAP_ATOMIC
#define DRM_CLIENT_CAP_ATOMIC 3
#endif

#ifndef DRM_IOCTL_MODE_DESTROY_BLOB
#ifdef DRM_IOCTL_MODE_DESTROYPROPBLOB
#define DRM_IOCTL_MODE_DESTROY_BLOB DRM_IOCTL_MODE_DESTROYPROPBLOB
#endif
#endif

#ifndef DRM_MODE_OBJECT_PLANE
#define DRM_MODE_OBJECT_PLANE 0x53524150
#endif

#define W 320
#define H 480
#define BPP 16
#define NBUFS 2
#define TOUCH_SLOTS 2
#define TOUCH_TRAIL 512

static volatile sig_atomic_t g_stop;

static void on_sig(int sig)
{
	(void)sig;
	g_stop = 1;
}

static int g_logfd = -1;

static void log_open(void)
{
	if (g_logfd >= 0)
		return;
	g_logfd = open("/root/qpic_demo.log",
		       O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
}

static void logline(const char *fmt, ...)
{
	char buf[1024];
	char ts[64] = "[--:--:--.---] ";
	char line[1200];
	struct timeval tv;
	struct tm tmv;
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n >= (int)sizeof(buf))
		n = (int)sizeof(buf) - 1;

	if (gettimeofday(&tv, NULL) == 0 && localtime_r(&tv.tv_sec, &tmv) != NULL) {
		snprintf(ts, sizeof(ts), "[%02d:%02d:%02d.%03ld] ",
			 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (long)tv.tv_usec / 1000);
	}

	n = snprintf(line, sizeof(line), "%s%.*s\n", ts, n, buf);
	if (n < 0)
		return;
	if (n >= (int)sizeof(line))
		n = (int)sizeof(line) - 1;

	(void)!write(2, line, (size_t)n);
	log_open();
	if (g_logfd >= 0)
		(void)!write(g_logfd, line, (size_t)n);
}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static const char *GLYPH[] = {
	"01110""10001""10001""10001""10001""10001""01110",
	"00100""01100""00100""00100""00100""00100""01110",
	"01110""10001""00001""00110""01000""10000""11111",
	"01110""10001""00001""00110""00001""10001""01110",
	"00010""00110""01010""10010""11111""00010""00010",
	"11111""10000""11110""00001""00001""10001""01110",
	"00110""01000""10000""11110""10001""10001""01110",
	"11111""00001""00010""00100""01000""01000""01000",
	"01110""10001""10001""01110""10001""10001""01110",
	"01110""10001""10001""01111""00001""00010""01100",
	"00000""00100""00100""00000""00100""00100""00000",
	"00000""00000""00000""00000""00000""00000""00000",
	"10001""10001""10001""11111""10001""10001""10001",
	"11111""10000""10000""11110""10000""10000""11111",
	"10000""10000""10000""10000""10000""10000""11111",
	"01110""10001""10001""10001""10001""10001""01110",
	"11110""10001""10001""11110""10000""10000""10000",
	"11110""10001""10001""11110""10100""10010""10001",
	"01111""10000""10000""01110""00001""00001""11110",
	"10001""10001""10001""10101""10101""10101""01010",
	"01110""10001""10001""11111""10001""10001""10001",
	"01110""00100""00100""00100""00100""00100""01110",
	"11111""00100""00100""00100""00100""00100""00100",
	"10001""11001""10101""10011""10001""10001""10001",
	"10001""10010""10100""11000""10100""10010""10001",
	"10001""10001""01010""00100""00100""00100""00100",
	"00100""00100""00100""00100""00100""00000""00100",
	/* B */ "11110""10001""10001""11110""10001""10001""11110",
	/* C */ "01110""10001""10000""10000""10000""10001""01110",
	/* D */ "11110""10001""10001""10001""10001""10001""11110",
	/* F */ "11111""10000""10000""11110""10000""10000""10000",
	/* G */ "01110""10001""10000""10111""10001""10001""01110",
	/* J */ "00111""00010""00010""00010""00010""10010""01100",
	/* M */ "10001""11011""10101""10001""10001""10001""10001",
	/* Q */ "01110""10001""10001""10001""10101""10010""01101",
	/* U */ "10001""10001""10001""10001""10001""10001""01110",
	/* V */ "10001""10001""10001""10001""10001""01010""00100",
	/* X */ "10001""10001""01010""00100""01010""10001""10001",
	/* Z */ "11111""00001""00010""00100""01000""10000""11111",
};

static int glyph_index(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c == ':')
		return 10;
	if (c == ' ')
		return 11;
	switch (c) {
	case 'H': case 'h': return 12;
	case 'E': case 'e': return 13;
	case 'L': case 'l': return 14;
	case 'O': case 'o': return 15;
	case 'P': case 'p': return 16;
	case 'R': case 'r': return 17;
	case 'S': case 's': return 18;
	case 'W': case 'w': return 19;
	case 'A': case 'a': return 20;
	case 'I': case 'i': return 21;
	case 'T': case 't': return 22;
	case 'N': case 'n': return 23;
	case 'K': case 'k': return 24;
	case 'Y': case 'y': return 25;
	case '!': return 26;
	case 'B': case 'b': return 27;
	case 'C': case 'c': return 28;
	case 'D': case 'd': return 29;
	case 'F': case 'f': return 30;
	case 'G': case 'g': return 31;
	case 'J': case 'j': return 32;
	case 'M': case 'm': return 33;
	case 'Q': case 'q': return 34;
	case 'U': case 'u': return 35;
	case 'V': case 'v': return 36;
	case 'X': case 'x': return 37;
	case 'Z': case 'z': return 38;
	default: return 11;
	}
}

struct drm_buf {
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
	uint32_t fb_id;
	uint16_t *map;
};

struct drm_props {
	uint32_t plane_fb_id;
	uint32_t plane_crtc_id;
	uint32_t src_x, src_y, src_w, src_h;
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
	uint32_t crtc_mode_id;
	uint32_t crtc_active;
	uint32_t conn_crtc_id;
};

struct qpic_ctx {
	int fd;
	uint32_t conn_id;
	uint32_t crtc_id;
	uint32_t plane_id;
	uint32_t mode_blob_id;
	struct drm_mode_modeinfo mode;
	struct drm_props props;
	struct drm_buf bufs[NBUFS];
	int cur_buf;
	int first_commit;
};

static int drm_ioctl(int fd, unsigned long req, void *arg)
{
	int ret;
	do {
		ret = ioctl(fd, req, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	return ret;
}

static int drm_set_client_cap(int fd, uint64_t cap, uint64_t val)
{
	struct drm_set_client_cap arg = { .capability = cap, .value = val };
	return drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &arg);
}

static int drm_get_cap(int fd, uint64_t cap, uint64_t *val)
{
	struct drm_get_cap arg = { .capability = cap, .value = 0 };
	if (drm_ioctl(fd, DRM_IOCTL_GET_CAP, &arg) < 0)
		return -1;
	*val = arg.value;
	return 0;
}

static int drm_find_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *want)
{
	struct drm_mode_obj_get_properties gprops;
	struct drm_mode_get_property gprop;
	uint32_t *ids = NULL;
	uint64_t *vals = NULL;
	uint32_t count = 0;
	int i, ret = -1;

	for (;;) {
		memset(&gprops, 0, sizeof(gprops));
		gprops.obj_id = obj_id;
		gprops.obj_type = obj_type;
		gprops.count_props = count;
		if (count) {
			gprops.props_ptr = (uint64_t)(uintptr_t)ids;
			gprops.prop_values_ptr = (uint64_t)(uintptr_t)vals;
		}
		if (drm_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gprops) < 0)
			goto out;
		if (gprops.count_props <= count)
			break;
		count = gprops.count_props;
		free(ids);
		free(vals);
		ids = calloc(count, sizeof(uint32_t));
		vals = calloc(count, sizeof(uint64_t));
		if (!ids || !vals)
			goto out;
	}

	for (i = 0; i < (int)count; i++) {
		memset(&gprop, 0, sizeof(gprop));
		gprop.prop_id = ids[i];
		if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &gprop) < 0)
			continue;
		if (strncmp(gprop.name, want, sizeof(gprop.name)) == 0) {
			ret = (int)ids[i];
			break;
		}
	}

out:
	free(ids);
	free(vals);
	return ret;
}

static int drm_map_props(int fd, struct qpic_ctx *d)
{
	struct drm_props *p = &d->props;
	const struct { const char *name; uint32_t obj; uint32_t type; uint32_t *dst; } tbl[] = {
		{ "FB_ID",    d->plane_id, DRM_MODE_OBJECT_PLANE, &p->plane_fb_id },
		{ "CRTC_ID",  d->plane_id, DRM_MODE_OBJECT_PLANE, &p->plane_crtc_id },
		{ "SRC_X",    d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_x },
		{ "SRC_Y",    d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_y },
		{ "SRC_W",    d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_w },
		{ "SRC_H",    d->plane_id, DRM_MODE_OBJECT_PLANE, &p->src_h },
		{ "CRTC_X",   d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_x },
		{ "CRTC_Y",   d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_y },
		{ "CRTC_W",   d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_w },
		{ "CRTC_H",   d->plane_id, DRM_MODE_OBJECT_PLANE, &p->crtc_h },
		{ "MODE_ID",  d->crtc_id,  DRM_MODE_OBJECT_CRTC,  &p->crtc_mode_id },
		{ "ACTIVE",   d->crtc_id,  DRM_MODE_OBJECT_CRTC,  &p->crtc_active },
		{ "CRTC_ID",  d->conn_id,  DRM_MODE_OBJECT_CONNECTOR, &p->conn_crtc_id },
	};
	size_t i;

	for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
		int pid = drm_find_prop_id(fd, tbl[i].obj, tbl[i].type, tbl[i].name);
		if (pid < 0) {
			logline("property %s not found on obj %u", tbl[i].name, tbl[i].obj);
			return -1;
		}
		*tbl[i].dst = (uint32_t)pid;
		logline("prop %s id=%u", tbl[i].name, (uint32_t)pid);
	}
	return 0;
}

static int crtc_index_of(uint32_t *crtc_ids, uint32_t count, uint32_t crtc_id)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		if (crtc_ids[i] == crtc_id)
			return (int)i;
	}
	return -1;
}

static int drm_find_objects(struct qpic_ctx *d)
{
	struct drm_mode_card_res res;
	struct drm_mode_get_plane_res pres;
	struct drm_mode_modeinfo tmp_mode;
	uint32_t *conn_ids = NULL, *crtc_ids = NULL, *plane_ids = NULL;
	int i, j, ok = -1;
	int crtc_idx = -1;

	memset(&res, 0, sizeof(res));
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
		logline("GETRESOURCES probe: %s", strerror(errno));
		return -1;
	}
	logline("GETRESOURCES: %u connectors, %u crtcs, %u encoders",
		res.count_connectors, res.count_crtcs, res.count_encoders);
	if (!res.count_connectors || !res.count_crtcs)
		return -1;

	conn_ids = calloc(res.count_connectors, sizeof(uint32_t));
	crtc_ids = calloc(res.count_crtcs, sizeof(uint32_t));
	plane_ids = NULL;
	if (!conn_ids || !crtc_ids)
		goto out;

	res.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtc_ids;
	if (res.count_encoders) {
		uint32_t *enc_ids = calloc(res.count_encoders, sizeof(uint32_t));
		if (!enc_ids)
			goto out;
		res.encoder_id_ptr = (uint64_t)(uintptr_t)enc_ids;
	}
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
		logline("GETRESOURCES fill: %s", strerror(errno));
		goto out;
	}
	if (res.encoder_id_ptr)
		free((void *)(uintptr_t)res.encoder_id_ptr);

	for (i = 0; i < (int)res.count_crtcs; i++)
		logline("crtc[%d] id=%u", i, crtc_ids[i]);

	for (i = 0; i < (int)res.count_connectors; i++) {
		struct drm_mode_get_connector c;
		struct drm_mode_modeinfo *modes = NULL;
		uint32_t *encs = NULL;
		uint32_t prev_modes = 0;

		memset(&c, 0, sizeof(c));
		c.connector_id = conn_ids[i];
		c.count_modes = 1;
		c.modes_ptr = (uint64_t)(uintptr_t)&tmp_mode;
		if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) {
			logline("conn %u GETCONNECTOR probe: %s", conn_ids[i], strerror(errno));
			continue;
		}
		logline("conn %u type=%u conn=%u modes=%u encoders=%u props=%u encoder_id=%u",
			c.connector_id, c.connector_type, c.connection,
			c.count_modes, c.count_encoders, c.count_props, c.encoder_id);
		if (c.connection != DRM_MODE_CONNECTED || !c.count_modes)
			continue;

		do {
			uint32_t *props = NULL;
			uint64_t *prop_vals = NULL;

			prev_modes = c.count_modes;
			free(modes);
			free(encs);
			modes = calloc(c.count_modes, sizeof(*modes));
			encs = calloc(c.count_encoders ? c.count_encoders : 1, sizeof(uint32_t));
			if (!modes || !encs)
				goto next_conn;
			if (c.count_props) {
				props = calloc(c.count_props, sizeof(uint32_t));
				prop_vals = calloc(c.count_props, sizeof(uint64_t));
				if (!props || !prop_vals) {
					free(props);
					free(prop_vals);
					goto next_conn;
				}
			}

			c.modes_ptr = (uint64_t)(uintptr_t)modes;
			c.encoders_ptr = (uint64_t)(uintptr_t)encs;
			c.props_ptr = props ? (uint64_t)(uintptr_t)props : 0;
			c.prop_values_ptr = prop_vals ? (uint64_t)(uintptr_t)prop_vals : 0;
			if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) {
				logline("conn %u GETCONNECTOR fill: %s", conn_ids[i], strerror(errno));
				free(props);
				free(prop_vals);
				goto next_conn;
			}
			free(props);
			free(prop_vals);
		} while (c.count_modes != prev_modes);

		for (j = 0; j < (int)c.count_modes; j++) {
			if (modes[j].hdisplay == W && modes[j].vdisplay == H)
				break;
		}
		if (j == (int)c.count_modes)
			j = 0;

		memcpy(&d->mode, &modes[j], sizeof(d->mode));
		d->conn_id = c.connector_id;
		logline("selected mode %ux%u@%u on conn %u",
			d->mode.hdisplay, d->mode.vdisplay, d->mode.vrefresh, d->conn_id);

		{
			struct drm_mode_get_encoder enc;
			uint32_t enc_id = c.encoder_id ? c.encoder_id :
				(c.count_encoders ? encs[0] : 0);

			d->crtc_id = 0;
			if (enc_id) {
				memset(&enc, 0, sizeof(enc));
				enc.encoder_id = enc_id;
				if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
					if (enc.crtc_id)
						d->crtc_id = enc.crtc_id;
					else {
						for (j = 0; j < (int)res.count_crtcs; j++) {
							if (enc.possible_crtcs & (1u << j)) {
								d->crtc_id = crtc_ids[j];
								break;
							}
						}
					}
				}
			}
			if (!d->crtc_id && res.count_crtcs)
				d->crtc_id = crtc_ids[0];
		}

		crtc_idx = crtc_index_of(crtc_ids, res.count_crtcs, d->crtc_id);
		logline("crtc_id=%u (index %d)", d->crtc_id, crtc_idx);
		if (crtc_idx < 0)
			goto next_conn;

		memset(&pres, 0, sizeof(pres));
		if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0 || !pres.count_planes) {
			logline("GETPLANERESOURCES: %s count=%u",
				strerror(errno), pres.count_planes);
			goto next_conn;
		}

		free(plane_ids);
		plane_ids = calloc(pres.count_planes, sizeof(uint32_t));
		if (!plane_ids)
			goto next_conn;
		pres.plane_id_ptr = (uint64_t)(uintptr_t)plane_ids;
		if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0) {
			logline("GETPLANERESOURCES fill: %s", strerror(errno));
			goto next_conn;
		}
		logline("%u planes", pres.count_planes);

		for (j = 0; j < (int)pres.count_planes; j++) {
			struct drm_mode_get_plane pl;
			uint32_t *formats = NULL;
			int k;

			memset(&pl, 0, sizeof(pl));
			pl.plane_id = plane_ids[j];
			if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANE, &pl) < 0) {
				logline("plane %u GETPLANE probe: %s", plane_ids[j], strerror(errno));
				continue;
			}
			logline("plane %u possible_crtcs=0x%x count_formats=%u",
				plane_ids[j], pl.possible_crtcs, pl.count_format_types);
			if (!(pl.possible_crtcs & (1u << crtc_idx)))
				continue;

			formats = calloc(pl.count_format_types ? pl.count_format_types : 1, sizeof(uint32_t));
			if (!formats)
				continue;
			pl.format_type_ptr = (uint64_t)(uintptr_t)formats;
			if (drm_ioctl(d->fd, DRM_IOCTL_MODE_GETPLANE, &pl) < 0) {
				logline("plane %u GETPLANE fill: %s", plane_ids[j], strerror(errno));
				free(formats);
				continue;
			}
			for (k = 0; k < (int)pl.count_format_types; k++) {
				logline("  plane %u fmt=0x%08x", plane_ids[j], formats[k]);
				if (formats[k] == DRM_FORMAT_RGB565) {
					d->plane_id = plane_ids[j];
					logline("plane %u supports RGB565", d->plane_id);
					free(formats);
					ok = 0;
					goto out;
				}
			}
			free(formats);
		}
		continue;

next_conn:
		free(modes);
		free(encs);
		modes = NULL;
		encs = NULL;
		d->crtc_id = 0;
	}

out:
	free(conn_ids);
	free(crtc_ids);
	free(plane_ids);
	if (ok == 0)
		logline("conn=%u crtc=%u plane=%u mode=%ux%u",
			d->conn_id, d->crtc_id, d->plane_id,
			d->mode.hdisplay, d->mode.vdisplay);
	return ok;
}

static int drm_create_mode_blob(struct qpic_ctx *d)
{
	struct drm_mode_create_blob blob;

	memset(&blob, 0, sizeof(blob));
	blob.length = sizeof(d->mode);
	blob.data = (uint64_t)(uintptr_t)&d->mode;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob) < 0) {
		logline("CREATEPROPBLOB: %s", strerror(errno));
		return -1;
	}
	d->mode_blob_id = blob.blob_id;
	logline("mode blob id=%u", d->mode_blob_id);
	return 0;
}

static int drm_buf_create(struct qpic_ctx *d, struct drm_buf *b)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	struct drm_mode_fb_cmd2 fb;

	memset(&creq, 0, sizeof(creq));
	creq.width = W;
	creq.height = H;
	creq.bpp = BPP;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		logline("CREATE_DUMB: %s", strerror(errno));
		return -1;
	}
	b->handle = creq.handle;
	b->pitch = creq.pitch;
	b->size = creq.size;

	memset(&fb, 0, sizeof(fb));
	fb.width = W;
	fb.height = H;
	fb.pixel_format = DRM_FORMAT_RGB565;
	fb.handles[0] = b->handle;
	fb.pitches[0] = b->pitch;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) {
		logline("ADDFB2: %s", strerror(errno));
		return -1;
	}
	b->fb_id = fb.fb_id;

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = b->handle;
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		logline("MAP_DUMB: %s", strerror(errno));
		return -1;
	}
	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, mreq.offset);
	if (b->map == MAP_FAILED) {
		logline("mmap: %s", strerror(errno));
		b->map = NULL;
		return -1;
	}
	memset(b->map, 0, b->size);
	logline("buf fb_id=%u pitch=%u map=%p", b->fb_id, b->pitch, b->map);
	return 0;
}

static void drm_buf_destroy(struct qpic_ctx *d, struct drm_buf *b)
{
	if (b->map && b->map != MAP_FAILED) {
		munmap(b->map, b->size);
		b->map = NULL;
	}
	if (b->fb_id) {
		drm_ioctl(d->fd, DRM_IOCTL_MODE_RMFB, &b->fb_id);
		b->fb_id = 0;
	}
	if (b->handle) {
		struct drm_mode_destroy_dumb req = { .handle = b->handle };
		drm_ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &req);
		b->handle = 0;
	}
	(void)d;
}

static int drm_atomic_commit(struct qpic_ctx *d, struct drm_buf *b,
			     int x, int y, int w, int h, uint32_t flags)
{
	struct drm_mode_atomic atomic;
	struct drm_props *p = &d->props;
	uint32_t objs[32];
	uint32_t counts[32];
	uint32_t props[32];
	uint64_t values[32];
	int n = 0;
	uint32_t src_w = (uint32_t)w << 16;
	uint32_t src_h = (uint32_t)h << 16;

	#define ADD_PROP(obj, prop, val) do { \
		objs[n] = (obj); counts[n] = 1; props[n] = (prop); values[n] = (val); n++; \
	} while (0)

	ADD_PROP(d->plane_id, p->plane_fb_id, b->fb_id);
	ADD_PROP(d->plane_id, p->plane_crtc_id, d->crtc_id);
	ADD_PROP(d->plane_id, p->src_x, (uint32_t)x << 16);
	ADD_PROP(d->plane_id, p->src_y, (uint32_t)y << 16);
	ADD_PROP(d->plane_id, p->src_w, src_w);
	ADD_PROP(d->plane_id, p->src_h, src_h);
	ADD_PROP(d->plane_id, p->crtc_x, (uint32_t)x);
	ADD_PROP(d->plane_id, p->crtc_y, (uint32_t)y);
	ADD_PROP(d->plane_id, p->crtc_w, (uint32_t)w);
	ADD_PROP(d->plane_id, p->crtc_h, (uint32_t)h);

	if (d->first_commit) {
		ADD_PROP(d->crtc_id, p->crtc_active, 1);
		ADD_PROP(d->crtc_id, p->crtc_mode_id, d->mode_blob_id);
		ADD_PROP(d->conn_id, p->conn_crtc_id, d->crtc_id);
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	#undef ADD_PROP

	memset(&atomic, 0, sizeof(atomic));
	atomic.flags = flags;
	atomic.count_objs = (uint32_t)n;
	atomic.objs_ptr = (uint64_t)(uintptr_t)objs;
	atomic.count_props_ptr = (uint64_t)(uintptr_t)counts;
	atomic.props_ptr = (uint64_t)(uintptr_t)props;
	atomic.prop_values_ptr = (uint64_t)(uintptr_t)values;

	logline("atomic commit fb=%u area %d,%d %dx%d flags=0x%x", b->fb_id, x, y, w, h, flags);
	if (drm_ioctl(d->fd, DRM_IOCTL_MODE_ATOMIC, &atomic) < 0) {
		logline("ATOMIC: %s", strerror(errno));
		return -1;
	}
	d->first_commit = 0;
	logline("atomic commit ok");
	return 0;
}

static int drm_present(struct qpic_ctx *d)
{
	struct drm_buf *b = &d->bufs[d->cur_buf];
	return drm_atomic_commit(d, b, 0, 0, W, H, 0);
}

static void drm_destroy(struct qpic_ctx *d)
{
	int i;

	if (d->mode_blob_id) {
#ifdef DRM_IOCTL_MODE_DESTROY_BLOB
		struct drm_mode_destroy_blob req = { .blob_id = d->mode_blob_id };
		drm_ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_BLOB, &req);
#else
		(void)d;
#endif
		d->mode_blob_id = 0;
	}
	for (i = 0; i < NBUFS; i++)
		drm_buf_destroy(d, &d->bufs[i]);
	if (d->fd >= 0) {
		drm_ioctl(d->fd, DRM_IOCTL_DROP_MASTER, 0);
		close(d->fd);
		d->fd = -1;
	}
}

static int drm_init(struct qpic_ctx *d)
{
	uint64_t cap;
	const char *dev;
	int i;

	memset(d, 0, sizeof(*d));
	d->fd = -1;
	d->first_commit = 1;

	dev = getenv("DRM_CARD");
	if (!dev || !dev[0])
		dev = "/dev/dri/card0";

	d->fd = open(dev, O_RDWR | O_CLOEXEC);
	if (d->fd < 0) {
		logline("open %s: %s", dev, strerror(errno));
		return -1;
	}
	logline("opened %s", dev);

	if (drm_get_cap(d->fd, DRM_CAP_DUMB_BUFFER, &cap) < 0 || !cap) {
		logline("no dumb buffer support");
		return -1;
	}
	if (drm_set_client_cap(d->fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
		logline("SET_CLIENT_CAP atomic: %s", strerror(errno));
		return -1;
	}
	logline("atomic client cap ok");

	if (drm_ioctl(d->fd, DRM_IOCTL_SET_MASTER, 0) < 0)
		logline("SET_MASTER: %s (continuing)", strerror(errno));
	else
		logline("drm master acquired");

	if (drm_find_objects(d) < 0) {
		logline("find connector/plane failed");
		return -1;
	}
	if (drm_map_props(d->fd, d) < 0)
		return -1;
	if (drm_create_mode_blob(d) < 0)
		return -1;

	for (i = 0; i < NBUFS; i++) {
		if (drm_buf_create(d, &d->bufs[i]) < 0)
			return -1;
	}
	d->cur_buf = 0;
	logline("drm init ok, %d buffers", NBUFS);
	return 0;
}

static struct drm_buf *drm_draw_buf(struct qpic_ctx *d)
{
	return &d->bufs[d->cur_buf];
}

static int drm_flip(struct qpic_ctx *d)
{
	if (drm_present(d) < 0)
		return -1;
	d->cur_buf ^= 1;
	return 0;
}

static inline uint16_t *px(struct drm_buf *b, int x, int y)
{
	return (uint16_t *)((uint8_t *)b->map + y * b->pitch) + x;
}

static void fill(struct drm_buf *b, uint16_t c)
{
	int y, x;
	for (y = 0; y < H; y++) {
		uint16_t *row = (uint16_t *)((uint8_t *)b->map + y * b->pitch);
		for (x = 0; x < W; x++)
			row[x] = c;
	}
}

static void fill_rect(struct drm_buf *b, int x0, int y0, int x1, int y1, uint16_t c)
{
	int x, y;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > W) x1 = W;
	if (y1 > H) y1 = H;
	for (y = y0; y < y1; y++) {
		uint16_t *row = (uint16_t *)((uint8_t *)b->map + y * b->pitch);
		for (x = x0; x < x1; x++)
			row[x] = c;
	}
}

static void draw_pixel(struct drm_buf *b, int x, int y, uint16_t c)
{
	if (x >= 0 && x < W && y >= 0 && y < H)
		*px(b, x, y) = c;
}

static int iabs(int v)
{
	return v < 0 ? -v : v;
}

static void draw_line(struct drm_buf *b, int x0, int y0, int x1, int y1, uint16_t c)
{
	int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		draw_pixel(b, x0, y0, c);
		if (x0 == x1 && y0 == y1)
			break;
		{
			int e2 = 2 * err;
			if (e2 >= dy) {
				err += dy;
				x0 += sx;
			}
			if (e2 <= dx) {
				err += dx;
				y0 += sy;
			}
		}
	}
}

static void draw_line_w(struct drm_buf *b, int x0, int y0, int x1, int y1, uint16_t c)
{
	draw_line(b, x0, y0, x1, y1, c);
	draw_line(b, x0 + 1, y0, x1 + 1, y1, c);
	draw_line(b, x0, y0 + 1, x1, y1 + 1, c);
}

static void fill_circle(struct drm_buf *b, int cx, int cy, int r, uint16_t c)
{
	int y, x, r2 = r * r;

	for (y = -r; y <= r; y++) {
		for (x = -r; x <= r; x++) {
			if (x * x + y * y <= r2)
				draw_pixel(b, cx + x, cy + y, c);
		}
	}
}

static void draw_crosshair(struct drm_buf *b, int x, int y, uint16_t dim, uint16_t bright)
{
	draw_line(b, 0, y, W - 1, y, dim);
	draw_line(b, x, 0, x, H - 1, dim);
	draw_line(b, x - 12, y, x + 12, y, bright);
	draw_line(b, x, y - 12, x, y + 12, bright);
}

static void draw_char(struct drm_buf *b, int ox, int oy, char ch, int scale, uint16_t fg, uint16_t bg)
{
	int gi = glyph_index(ch);
	const char *g = GLYPH[gi];
	int cw = 5 * scale;
	int ch_h = 7 * scale;
	int ry, rx, sy, sx;

	fill_rect(b, ox, oy, ox + cw, oy + ch_h, bg);
	for (ry = 0; ry < 7; ry++) {
		for (rx = 0; rx < 5; rx++) {
			if (g[ry * 5 + rx] != '1')
				continue;
			for (sy = 0; sy < scale; sy++) {
				for (sx = 0; sx < scale; sx++) {
					int x = ox + rx * scale + sx;
					int y = oy + ry * scale + sy;
					if (x >= 0 && x < W && y >= 0 && y < H)
						*px(b, x, y) = fg;
				}
			}
		}
	}
}

static void draw_text(struct drm_buf *b, int ox, int oy, const char *s, int scale, uint16_t fg, uint16_t bg);

static int text_width(const char *s, int scale)
{
	int n = 0;
	while (s[n])
		n++;
	if (!n)
		return 0;
	return n * (5 * scale + scale) - scale;
}

static void draw_text_centered(struct drm_buf *b, int y, const char *s, int scale,
			       uint16_t fg, uint16_t bg)
{
	int tw = text_width(s, scale);
	int ox = (W - tw) / 2;
	if (ox < 0)
		ox = 0;
	draw_text(b, ox, y, s, scale, fg, bg);
}

static void draw_text(struct drm_buf *b, int ox, int oy, const char *s, int scale, uint16_t fg, uint16_t bg)
{
	int spacing = scale;
	while (*s) {
		draw_char(b, ox, oy, *s, scale, fg, bg);
		ox += 5 * scale + spacing;
		s++;
	}
}

static void draw_mosaic(struct drm_buf *b)
{
	static const uint16_t palette[] = {
		0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f, 0x07ff, 0xffff, 0x0000,
		0x8410, 0xfc00, 0x03e0, 0x0010
	};
	int tile = 20;
	int ty, tx, i = 0;

	for (ty = 0; ty < H; ty += tile) {
		for (tx = 0; tx < W; tx += tile) {
			uint16_t c = palette[i++ % (int)(sizeof(palette) / sizeof(palette[0]))];
			fill_rect(b, tx, ty, tx + tile, ty + tile, c);
		}
		i += 3;
	}
	fill_rect(b, 0, 0, W, 4, 0xffff);
	fill_rect(b, 0, H - 4, W, H, 0xffff);
	fill_rect(b, 0, 0, 4, H, 0xffff);
	fill_rect(b, W - 4, 0, W, H, 0xffff);
	fill_rect(b, 20, 200, 300, 280, 0x0000);
	draw_text_centered(b, 220, "HELLO", 6, 0xffff, 0x0000);
	draw_text_centered(b, 300, "QPIC DRM", 3, 0xffff, rgb565(0, 0, 80));
}

static uint16_t maybe_inv(uint16_t c, int inv)
{
	return inv ? (uint16_t)(c ^ 0xffff) : c;
}

static void draw_clock(struct drm_buf *b, int inv)
{
	time_t now = time(NULL);
	struct tm tm;
	char buf[16];
	uint16_t bg = maybe_inv(rgb565(10, 20, 40), inv);
	uint16_t bar = maybe_inv(rgb565(0, 80, 160), inv);
	uint16_t fg = maybe_inv(0xffff, inv);
	uint16_t dim = maybe_inv(rgb565(200, 200, 200), inv);

	localtime_r(&now, &tm);
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

	fill(b, bg);
	fill_rect(b, 10, 10, W - 10, 50, bar);
	draw_text_centered(b, 18, "TIME", 4, fg, bar);
	draw_text_centered(b, 200, buf, 6, fg, bg);
	draw_text_centered(b, 400, "POWER INVERT", 2, dim, bg);
}

static int open_pwrkey(void)
{
	const char *cands[] = { "/dev/input/event0", "/dev/input/event1", "/dev/input/event2", NULL };
	int i;
	for (i = 0; cands[i]; i++) {
		int fd = open(cands[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0)
			return fd;
	}
	return -1;
}

static int wait_power_key(int timeout_ms)
{
	int fd = open_pwrkey();
	struct pollfd pfd;
	struct input_event ev;
	int elapsed = 0;
	const int step = 50;

	if (fd < 0) {
		logline("no input device, sleep %d ms instead", timeout_ms);
		while (elapsed < timeout_ms && !g_stop) {
			usleep(step * 1000);
			elapsed += step;
		}
		return g_stop ? -1 : 0;
	}

	while (read(fd, &ev, sizeof(ev)) == sizeof(ev))
		;

	pfd.fd = fd;
	pfd.events = POLLIN;
	logline("waiting for KEY_POWER (timeout %d ms)...", timeout_ms);

	while (!g_stop && (timeout_ms < 0 || elapsed < timeout_ms)) {
		int pr = poll(&pfd, 1, step);
		elapsed += step;
		if (pr <= 0)
			continue;
		while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
			if (ev.type == EV_KEY && ev.value == 1) {
				logline("key code=%d pressed", ev.code);
				close(fd);
				return 0;
			}
		}
	}
	close(fd);
	return g_stop ? -1 : 1;
}

static void sleep_ms(int ms)
{
	int left = ms;
	while (left > 0 && !g_stop) {
		int chunk = left > 100 ? 100 : left;
		usleep(chunk * 1000);
		left -= chunk;
	}
}

struct touch_slot {
	int tracking_id;
	int raw_x, raw_y;
	int x, y;
	int have_pos;
	int n;
	int16_t trail_x[TOUCH_TRAIL];
	int16_t trail_y[TOUCH_TRAIL];
};

struct touch_dev {
	int fd;
	int cur_slot;
	int swap_xy;
	struct input_absinfo abs_x;
	struct input_absinfo abs_y;
	struct touch_slot slots[TOUCH_SLOTS];
	uint8_t moved[TOUCH_SLOTS];
};

static int map_axis(int v, const struct input_absinfo *a, int out_max)
{
	int span = a->maximum - a->minimum;
	if (span <= 0)
		return 0;
	if (v < a->minimum)
		v = a->minimum;
	if (v > a->maximum)
		v = a->maximum;
	return (v - a->minimum) * out_max / span;
}

static void touch_map_xy(const struct touch_dev *t, int raw_x, int raw_y, int *ox, int *oy)
{
	if (t->swap_xy) {
		*ox = map_axis(raw_y, &t->abs_y, W - 1);
		*oy = map_axis(raw_x, &t->abs_x, H - 1);
	} else {
		*ox = map_axis(raw_x, &t->abs_x, W - 1);
		*oy = map_axis(raw_y, &t->abs_y, H - 1);
	}
}

static int open_touch_dev(void)
{
	char path[64];
	char name[256];
	int i, fd, fallback = -1;

	for (i = 0; i < 8; i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
			name[0] = 0;
		logline("input %s name=\"%s\"", path, name);
		if (strstr(name, "sitronix") || strstr(name, "Touch") ||
		    strstr(name, "touch")) {
			return fd;
		}
		if (i == 3 && fallback < 0)
			fallback = fd;
		else
			close(fd);
	}
	if (fallback >= 0)
		logline("touch: using event3 fallback");
	return fallback;
}

static void touch_slot_reset(struct touch_slot *s)
{
	s->tracking_id = -1;
	s->raw_x = 0;
	s->raw_y = 0;
	s->x = 0;
	s->y = 0;
	s->have_pos = 0;
	s->n = 0;
}

static void touch_trail_clear(struct touch_slot *s)
{
	s->n = 0;
}

static void touch_trail_add(struct touch_slot *s, int x, int y)
{
	if (s->n > 0 && s->trail_x[s->n - 1] == x && s->trail_y[s->n - 1] == y)
		return;
	if (s->n < TOUCH_TRAIL) {
		s->trail_x[s->n] = (int16_t)x;
		s->trail_y[s->n] = (int16_t)y;
		s->n++;
		return;
	}
	memmove(s->trail_x, s->trail_x + 1, (TOUCH_TRAIL - 1) * sizeof(s->trail_x[0]));
	memmove(s->trail_y, s->trail_y + 1, (TOUCH_TRAIL - 1) * sizeof(s->trail_y[0]));
	s->trail_x[TOUCH_TRAIL - 1] = (int16_t)x;
	s->trail_y[TOUCH_TRAIL - 1] = (int16_t)y;
}

static int touch_init(struct touch_dev *t)
{
	int i;
	int xspan, yspan;
	struct input_event ev;

	memset(t, 0, sizeof(*t));
	t->fd = -1;
	t->cur_slot = 0;
	for (i = 0; i < TOUCH_SLOTS; i++)
		touch_slot_reset(&t->slots[i]);

	t->fd = open_touch_dev();
	if (t->fd < 0) {
		logline("touch: no input device");
		return -1;
	}

	memset(&t->abs_x, 0, sizeof(t->abs_x));
	memset(&t->abs_y, 0, sizeof(t->abs_y));
	if (ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_X), &t->abs_x) < 0 ||
	    ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_Y), &t->abs_y) < 0) {
		logline("touch: EVIOCGABS MT failed (%s), trying ABS_X/Y", strerror(errno));
		if (ioctl(t->fd, EVIOCGABS(ABS_X), &t->abs_x) < 0 ||
		    ioctl(t->fd, EVIOCGABS(ABS_Y), &t->abs_y) < 0) {
			logline("touch: no abs axes");
			close(t->fd);
			t->fd = -1;
			return -1;
		}
	}

	xspan = t->abs_x.maximum - t->abs_x.minimum;
	yspan = t->abs_y.maximum - t->abs_y.minimum;
	t->swap_xy = (xspan > yspan && W < H) ? 1 : 0;
	logline("touch abs X=%d..%d Y=%d..%d swap_xy=%d",
		t->abs_x.minimum, t->abs_x.maximum,
		t->abs_y.minimum, t->abs_y.maximum, t->swap_xy);

	while (read(t->fd, &ev, sizeof(ev)) == sizeof(ev))
		;
	return 0;
}

static void touch_commit_slot(struct touch_dev *t, int slot)
{
	struct touch_slot *s;

	if (slot < 0 || slot >= TOUCH_SLOTS || !t->moved[slot])
		return;
	s = &t->slots[slot];
	touch_map_xy(t, s->raw_x, s->raw_y, &s->x, &s->y);
	s->have_pos = 1;
	if (s->tracking_id >= 0)
		touch_trail_add(s, s->x, s->y);
	t->moved[slot] = 0;
}

static int touch_handle_event(struct touch_dev *t, const struct input_event *ev)
{
	int slot = t->cur_slot;
	struct touch_slot *s;
	int i, dirty = 0;

	if (ev->type == EV_ABS) {
		if (ev->code == ABS_MT_SLOT) {
			if (ev->value >= 0 && ev->value < TOUCH_SLOTS)
				t->cur_slot = ev->value;
			return 0;
		}
		if (slot < 0 || slot >= TOUCH_SLOTS)
			return 0;
		s = &t->slots[slot];
		switch (ev->code) {
		case ABS_MT_TRACKING_ID:
			if (ev->value < 0) {
				s->tracking_id = -1;
			} else {
				if (s->tracking_id < 0)
					touch_trail_clear(s);
				s->tracking_id = ev->value;
			}
			return 1;
		case ABS_MT_POSITION_X:
		case ABS_X:
			s->raw_x = ev->value;
			t->moved[slot] = 1;
			return 0;
		case ABS_MT_POSITION_Y:
		case ABS_Y:
			s->raw_y = ev->value;
			t->moved[slot] = 1;
			return 0;
		default:
			return 0;
		}
	}

	if (ev->type == EV_KEY && ev->code == BTN_TOUCH && ev->value == 0) {
		for (i = 0; i < TOUCH_SLOTS; i++)
			t->slots[i].tracking_id = -1;
		return 1;
	}

	if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
		for (i = 0; i < TOUCH_SLOTS; i++) {
			if (t->moved[i]) {
				touch_commit_slot(t, i);
				dirty = 1;
			}
		}
		return dirty;
	}

	if (ev->type == EV_SYN && ev->code == SYN_DROPPED)
		logline("touch: SYN_DROPPED");
	return 0;
}

static const uint16_t SLOT_COL[TOUCH_SLOTS] = { 0x07ff, 0xffe0 };

static void draw_touch_frame(struct drm_buf *b, const struct touch_dev *t)
{
	static const uint16_t bg = 0x0000;
	static const uint16_t bar = 0x10a4;
	char line[32];
	int i, k, nact = 0;
	const struct touch_slot *first = NULL;

	fill(b, bg);
	fill_rect(b, 0, 0, W, 56, bar);

	for (i = 0; i < TOUCH_SLOTS; i++) {
		const struct touch_slot *s = &t->slots[i];
		uint16_t col = SLOT_COL[i];

		for (k = 1; k < s->n; k++)
			draw_line_w(b, s->trail_x[k - 1], s->trail_y[k - 1],
				    s->trail_x[k], s->trail_y[k], col);

		if (s->tracking_id < 0 || !s->have_pos)
			continue;
		if (!first)
			first = s;
		nact++;
		draw_crosshair(b, s->x, s->y, rgb565(50, 50, 70), 0xffff);
		fill_circle(b, s->x, s->y, 7, col);
		fill_circle(b, s->x, s->y, 3, 0xffff);
	}

	draw_text_centered(b, 6, "TOUCH", 3, 0xffff, bar);
	if (first) {
		snprintf(line, sizeof(line), "X %3d Y %3d N %d",
			 first->x, first->y, nact);
	} else {
		snprintf(line, sizeof(line), "DRAW PATH  N 0");
	}
	draw_text_centered(b, 34, line, 2, rgb565(180, 220, 255), bar);
	draw_text_centered(b, 452, "POWER TO EXIT", 2, rgb565(160, 160, 160), bg);
}

static int pwrkey_pressed(int fd)
{
	struct input_event ev;

	if (fd < 0)
		return 0;
	while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if (ev.type == EV_KEY && ev.value == 1) {
			logline("key code=%d pressed", ev.code);
			return 1;
		}
	}
	return 0;
}

static int run_touch_trace(struct qpic_ctx *d)
{
	struct touch_dev t;
	struct drm_buf *db;
	struct pollfd pfds[2];
	int pwr = -1, nfds, dirty = 1;
	struct input_event ev;

	logline("touch pointer-location: draw path, power key exits");
	if (touch_init(&t) < 0) {
		db = drm_draw_buf(d);
		fill(db, 0x0000);
		draw_text_centered(db, 200, "NO TOUCH DEV", 2, 0xffff, 0x0000);
		draw_text_centered(db, 280, "POWER TO EXIT", 2, rgb565(255, 220, 0), 0x0000);
		if (drm_flip(d) == 0)
			wait_power_key(-1);
		return -1;
	}

	pwr = open_pwrkey();
	if (pwr >= 0) {
		while (read(pwr, &ev, sizeof(ev)) == sizeof(ev))
			;
	}

	while (!g_stop) {
		if (dirty) {
			db = drm_draw_buf(d);
			draw_touch_frame(db, &t);
			if (drm_flip(d) < 0)
				break;
			dirty = 0;
		}

		nfds = 0;
		pfds[nfds].fd = t.fd;
		pfds[nfds].events = POLLIN;
		nfds++;
		if (pwr >= 0) {
			pfds[nfds].fd = pwr;
			pfds[nfds].events = POLLIN;
			nfds++;
		}
		if (poll(pfds, nfds, 50) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (pwrkey_pressed(pwr))
			break;
		while (read(t.fd, &ev, sizeof(ev)) == sizeof(ev)) {
			if (touch_handle_event(&t, &ev))
				dirty = 1;
		}
	}

	if (pwr >= 0)
		close(pwr);
	if (t.fd >= 0)
		close(t.fd);
	return 0;
}

static int elapsed_ms(const struct timeval *t0)
{
	struct timeval now;
	long sec, usec;

	if (gettimeofday(&now, NULL) < 0)
		return 0;
	sec = now.tv_sec - t0->tv_sec;
	usec = now.tv_usec - t0->tv_usec;
	if (usec < 0) {
		sec--;
		usec += 1000000;
	}
	if (sec < 0)
		return 0;
	return (int)(sec * 1000 + usec / 1000);
}

static int run_clock(struct qpic_ctx *d, int clock_secs)
{
	struct drm_buf *db;
	struct timeval t0;
	struct pollfd pfd;
	int pwr, invert = 0, last_sec = -1, need_redraw = 1;
	int limit_ms = clock_secs * 1000;
	struct input_event ev;

	logline("clock for %d s (power key toggles invert)", clock_secs);
	pwr = open_pwrkey();
	if (pwr >= 0) {
		while (read(pwr, &ev, sizeof(ev)) == sizeof(ev))
			;
	} else {
		logline("clock: no pwrkey, invert disabled");
	}

	gettimeofday(&t0, NULL);
	pfd.fd = pwr;
	pfd.events = POLLIN;

	while (!g_stop && elapsed_ms(&t0) < limit_ms) {
		int now_sec = elapsed_ms(&t0) / 1000;

		if (need_redraw || now_sec != last_sec) {
			db = drm_draw_buf(d);
			draw_clock(db, invert);
			if (drm_flip(d) < 0)
				break;
			last_sec = now_sec;
			need_redraw = 0;
		}

		if (pwr < 0) {
			usleep(50 * 1000);
			continue;
		}
		if (poll(&pfd, 1, 50) > 0 && pwrkey_pressed(pwr)) {
			invert = !invert;
			need_redraw = 1;
			logline("clock invert=%d", invert);
		}
	}

	if (pwr >= 0)
		close(pwr);
	return 0;
}

int main(int argc, char **argv)
{
	struct qpic_ctx drm;
	struct drm_buf *db;
	int clock_secs = 15;
	int i;

	(void)argv;
	if (argc > 1)
		clock_secs = atoi(argv[1]);
	if (clock_secs < 3)
		clock_secs = 3;

	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	logline("qpic_drm_demo: atomic init...");
	if (drm_init(&drm) < 0) {
		drm_destroy(&drm);
		return 1;
	}

	{
		uint16_t colors[] = { rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255) };
		const char *names[] = { "RED", "GREEN", "BLUE" };
		for (i = 0; i < 3 && !g_stop; i++) {
			logline("solid: %s", names[i]);
			db = drm_draw_buf(&drm);
			fill(db, colors[i]);
			draw_text_centered(db, 220, names[i], 5, 0xffff, colors[i]);
			if (drm_flip(&drm) < 0)
				break;
			sleep_ms(1200);
		}
	}

	if (!g_stop) {
		logline("mosaic + HELLO");
		db = drm_draw_buf(&drm);
		draw_mosaic(db);
		if (drm_flip(&drm) < 0)
			goto done;
		sleep_ms(2500);
	}

	if (!g_stop)
		run_clock(&drm, clock_secs);

	if (!g_stop) {
		logline("touch pointer-location");
		run_touch_trace(&drm);
	}

done:
	logline("blank and exit");
	db = drm_draw_buf(&drm);
	fill(db, 0x0000);
	drm_flip(&drm);
	sleep_ms(300);

	drm_destroy(&drm);
	return 0;
}
