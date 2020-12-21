/*
 * jigsaw - a plug-in for GIMP
 *
 * Copyright (C) Nigel Wetten
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Contact info: nigel@cs.nwu.edu
 *
 * Version: 1.0.0
 *
 * Version: 1.0.1
 *
 * tim coppefield [timecop@japan.co.jp]
 *
 * Added dynamic preview mode.
 *
 * Damn, this plugin is the tightest piece of code I ever seen.
 * I wish all filters in the plugins operated on guchar *buffer
 * of the entire image :) sweet stuff.
 *
 */

#include "config.h"

#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define PLUG_IN_PROC   "plug-in-jigsaw"
#define PLUG_IN_BINARY "jigsaw"
#define PLUG_IN_ROLE   "gimp-jigsaw"


typedef enum
{
	BEZIER_1,
	BEZIER_2
} style_t;

typedef enum
{
	LEFT,
	RIGHT,
	UP,
	DOWN
} bump_t;


#define XFACTOR2 0.0833
#define XFACTOR3 0.2083
#define XFACTOR4 0.2500

#define XFACTOR5 0.2500
#define XFACTOR6 0.2083
#define XFACTOR7 0.0833

#define YFACTOR2 0.1000
#define YFACTOR3 0.2200
#define YFACTOR4 0.1000

#define YFACTOR5 0.1000
#define YFACTOR6 0.4666
#define YFACTOR7 0.1000
#define YFACTOR8 0.2000

#define MAX_VALUE 255
#define MIN_VALUE 0
#define DELTA 0.15

#define BLACK_R 30
#define BLACK_G 30
#define BLACK_B 30

#define WALL_XFACTOR2 0.05
#define WALL_XFACTOR3 0.05
#define WALL_YFACTOR2 0.05
#define WALL_YFACTOR3 0.05

#define WALL_XCONS2 0.2
#define WALL_XCONS3 0.3
#define WALL_YCONS2 0.2
#define WALL_YCONS3 0.3

#define FUDGE 1.2

#define MIN_XTILES 1
#define MAX_XTILES 20
#define MIN_YTILES 1
#define MAX_YTILES 20
#define MIN_BLEND_LINES 0
#define MAX_BLEND_LINES 10
#define MIN_BLEND_AMOUNT 0
#define MAX_BLEND_AMOUNT 1.0

#define DRAW_POINT(buffer, bufsize, index)         \
	do                                               \
	{                                              \
		if ((index) >= 0 && (index) + 2 < (bufsize)) \
		{                                          \
			buffer[(index) + 0] = BLACK_R;           \
			buffer[(index) + 1] = BLACK_G;           \
			buffer[(index) + 2] = BLACK_B;           \
		}                                          \
	}                                              \
	while (0)

#define DARKEN_POINT(buffer, bufsize, index, delta, temp)                \
	do                                                                     \
	{                                                                    \
		if ((index) >= 0 && (index) + 2 < (bufsize))                       \
		{                                                                \
			temp = MAX (buffer[(index) + 0] * (1.0 - (delta)), MIN_VALUE); \
			buffer[(index) + 0] = temp;                                    \
			temp = MAX (buffer[(index) + 1] * (1.0 - (delta)), MIN_VALUE); \
			buffer[(index) + 1] = temp;                                    \
			temp = MAX (buffer[(index) + 2] * (1.0 - (delta)), MIN_VALUE); \
			buffer[(index) + 2] = temp;                                    \
		}                                                                \
	}                                                                    \
	while (0)

#define LIGHTEN_POINT(buffer, bufsize, index, delta, temp)               \
	do                                                                     \
	{                                                                    \
		if ((index) >= 0 && (index) + 2 < (bufsize))                       \
		{                                                                \
			temp = MIN (buffer[(index) + 0] * (1.0 + (delta)), MAX_VALUE); \
			buffer[(index) + 0] = temp;                                    \
			temp = MIN (buffer[(index) + 1] * (1.0 + (delta)), MAX_VALUE); \
			buffer[(index) + 1] = temp;                                    \
			temp = MIN (buffer[(index) + 2] * (1.0 + (delta)), MAX_VALUE); \
			buffer[(index) + 2] = temp;                                    \
		}                                                                \
	}                                                                    \
	while (0)


typedef struct config_tag config_t;

struct config_tag
{
	gint x;
	gint y;
	style_t style;
	gint blend_lines;
	gdouble blend_amount;
};

typedef struct globals_tag globals_t;

struct globals_tag
{
	gint  *cachex1[4];
	gint  *cachex2[4];
	gint  *cachey1[4];
	gint  *cachey2[4];
	gint steps[4];
	gint  *gridx;
	gint  *gridy;
	gint **blend_outer_cachex1[4];
	gint **blend_outer_cachex2[4];
	gint **blend_outer_cachey1[4];
	gint **blend_outer_cachey2[4];
	gint **blend_inner_cachex1[4];
	gint **blend_inner_cachex2[4];
	gint **blend_inner_cachey1[4];
	gint **blend_inner_cachey2[4];
};


typedef struct _Jigsaw Jigsaw;
typedef struct _JigsawClass JigsawClass;

struct _Jigsaw
{
	GimpPlugIn parent_instance;
};

struct _JigsawClass
{
	GimpPlugInClass parent_class;
};


#define JIGSAW_TYPE  (jigsaw_get_type ())
#define JIGSAW (obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), JIGSAW_TYPE, Jigsaw))

GType                   jigsaw_get_type         (void) G_GNUC_CONST;

static GList          * jigsaw_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure  * jigsaw_create_procedure (GimpPlugIn           *plug_in,
                                                 const gchar          *name);

static GimpValueArray * jigsaw_run              (GimpProcedure        *procedure,
                                                 GimpRunMode run_mode,
                                                 GimpImage            *image,
                                                 GimpDrawable         *drawable,
                                                 const GimpValueArray *args,
                                                 gpointer run_data);

static void     jigsaw             (GimpDrawable *drawable,
                                    GimpPreview  *preview);
static void     jigsaw_preview     (GimpDrawable *drawable,
                                    GimpPreview  *preview);

static gboolean jigsaw_dialog                    (GimpDrawable        *drawable);
static void     jigsaw_scale_entry_update_double (GimpLabelSpin       *entry,
                                                  gdouble             *value);
static void     jigsaw_scale_entry_update_int    (GimpLabelSpin       *entry,
                                                  gint                *value);

static void     draw_jigsaw        (guchar    *buffer,
                                    gint bufsize,
                                    gint width,
                                    gint height,
                                    gint bytes,
                                    gboolean preview_mode);

static void draw_vertical_border   (guchar *buffer, gint bufsize,
                                    gint width, gint height,
                                    gint bytes, gint x_offset, gint ytiles,
                                    gint blend_lines, gdouble blend_amount);
static void draw_horizontal_border (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint xtiles,
                                    gint blend_lines, gdouble blend_amount);
static void draw_vertical_line     (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint px[2], gint py[2]);
static void draw_horizontal_line   (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint px[2], gint py[2]);
static void darken_vertical_line   (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint *px, gint *py, gdouble delta);
static void lighten_vertical_line  (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint *px, gint *py, gdouble delta);
static void darken_horizontal_line (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint *px, gint *py, gdouble delta);
static void lighten_horizontal_line(guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint *px, gint *py, gdouble delta);
static void draw_right_bump        (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint curve_start_offset,
                                    gint steps);
static void draw_left_bump         (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint curve_start_offset,
                                    gint steps);
static void draw_up_bump           (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint curve_start_offset,
                                    gint steps);
static void draw_down_bump         (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint curve_start_offset,
                                    gint steps);
static void darken_right_bump      (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void lighten_right_bump     (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void darken_left_bump       (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void lighten_left_bump      (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void darken_up_bump         (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint curve_start_offest,
                                    gint steps, gdouble delta, gint counter);
static void lighten_up_bump        (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void darken_down_bump       (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void lighten_down_bump      (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint y_offset, gint curve_start_offset,
                                    gint steps, gdouble delta, gint counter);
static void generate_grid          (gint width, gint height, gint xtiles, gint ytiles,
                                    gint *x, gint *y);
static void generate_bezier        (gint px[4], gint py[4], gint steps,
                                    gint *cachex, gint *cachey);
static void malloc_cache           (void);
static void free_cache             (void);
static void init_right_bump        (gint width, gint height);
static void init_left_bump         (gint width, gint height);
static void init_up_bump           (gint width, gint height);
static void init_down_bump         (gint width, gint height);
static void draw_bezier_line       (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint steps, gint *cx, gint *cy);
static void darken_bezier_line     (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint y_offset, gint steps,
                                    gint *cx, gint *cy, gdouble delta);
static void lighten_bezier_line    (guchar *buffer, gint bufsize,
                                    gint width, gint bytes,
                                    gint x_offset, gint y_offset, gint steps,
                                    gint *cx, gint *cy, gdouble delta);
static void draw_bezier_vertical_border   (guchar *buffer, gint bufsize,
                                           gint width, gint height,
                                           gint bytes,
                                           gint x_offset, gint xtiles,
                                           gint ytiles, gint blend_lines,
                                           gdouble blend_amount, gint steps);
static void draw_bezier_horizontal_border (guchar *buffer, gint bufsize,
                                           gint width, gint height,
                                           gint bytes,
                                           gint x_offset, gint xtiles,
                                           gint ytiles, gint blend_lines,
                                           gdouble blend_amount, gint steps);
static void check_config           (gint width, gint height);


G_DEFINE_TYPE (Jigsaw, jigsaw, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (JIGSAW_TYPE)


static config_t config =
{
	5,
	5,
	BEZIER_1,
	3,
	0.5
};

static globals_t globals;


static void
jigsaw_class_init (JigsawClass *klass)
{
	GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

	plug_in_class->query_procedures = jigsaw_query_procedures;
	plug_in_class->create_procedure = jigsaw_create_procedure;
}

static void
jigsaw_init (Jigsaw *jigsaw)
{
}

static GList *
jigsaw_query_procedures (GimpPlugIn *plug_in)
{
	return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static GimpProcedure *
jigsaw_create_procedure (GimpPlugIn  *plug_in,
                         const gchar *name)
{
	GimpProcedure *procedure = NULL;

	if (!strcmp (name, PLUG_IN_PROC))
	{
		procedure = gimp_image_procedure_new (plug_in, name,
		                                      GIMP_PDB_PROC_TYPE_PLUGIN,
		                                      jigsaw_run, NULL, NULL);

		gimp_procedure_set_image_types (procedure, "RGB*");

		gimp_procedure_set_menu_label (procedure, N_("_Jigsaw..."));
		gimp_procedure_add_menu_path (procedure,
		                              "<Image>/Filters/Render/Pattern");

		gimp_procedure_set_documentation (procedure,
		                                  N_("Add a jigsaw-puzzle pattern "
		                                     "to the image"),
		                                  "Jigsaw puzzle look",
		                                  name);
		gimp_procedure_set_attribution (procedure,
		                                "Nigel Wetten",
		                                "Nigel Wetten",
		                                "May 2000");

		GIMP_PROC_ARG_INT (procedure, "x",
		                   "X",
		                   "Number of tiles across",
		                   1, GIMP_MAX_IMAGE_SIZE, 5,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_INT (procedure, "y",
		                   "Y",
		                   "Number of tiles down",
		                   1, GIMP_MAX_IMAGE_SIZE, 5,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_INT (procedure, "style",
		                   "Style",
		                   "The style/shape of the jigsaw puzzle { 0, 1 }",
		                   0, 1, BEZIER_1,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_INT (procedure, "blend-lines",
		                   "Blend lines",
		                   "Number of lines for shading bevels",
		                   1, GIMP_MAX_IMAGE_SIZE, 3,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_DOUBLE (procedure, "blend-amount",
		                      "Blend amount",
		                      "The power of the light highlights",
		                      0, 5, 0.5,
		                      G_PARAM_READWRITE);
	}

	return procedure;
}

static GimpValueArray *
jigsaw_run (GimpProcedure        *procedure,
            GimpRunMode run_mode,
            GimpImage            *image,
            GimpDrawable         *drawable,
            const GimpValueArray *args,
            gpointer run_data)
{
	INIT_I18N ();
	gegl_init (NULL, NULL);

	switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
		gimp_get_data (PLUG_IN_PROC, &config);

		if (!jigsaw_dialog (drawable))
		{
			return gimp_procedure_new_return_values (procedure,
			                                         GIMP_PDB_CANCEL,
			                                         NULL);
		}
		break;

	case GIMP_RUN_NONINTERACTIVE:
		config.x            = GIMP_VALUES_GET_INT    (args, 0);
		config.y            = GIMP_VALUES_GET_INT    (args, 1);
		config.style        = GIMP_VALUES_GET_INT    (args, 2);
		config.blend_lines  = GIMP_VALUES_GET_INT    (args, 3);
		config.blend_amount = GIMP_VALUES_GET_DOUBLE (args, 4);
		break;

	case GIMP_RUN_WITH_LAST_VALS:
		gimp_get_data (PLUG_IN_PROC, &config);
		break;
	};

	gimp_progress_init (_("Assembling jigsaw"));

	jigsaw (drawable, NULL);

	if (run_mode != GIMP_RUN_NONINTERACTIVE)
		gimp_displays_flush ();

	if (run_mode == GIMP_RUN_INTERACTIVE)
		gimp_set_data (PLUG_IN_PROC, &config, sizeof (config));

	return gimp_procedure_new_return_values (procedure, GIMP_PDB_SUCCESS, NULL);
}

static void
jigsaw (GimpDrawable *drawable,
        GimpPreview  *preview)
{
	GeglBuffer *gegl_buffer = NULL;
	const Babl *format      = NULL;
	guchar     *buffer;
	gint width;
	gint height;
	gint bytes;
	gint buffer_size;

	if (preview)
	{
		gimp_preview_get_size (preview, &width, &height);
		buffer = gimp_drawable_get_thumbnail_data (drawable,
		                                           &width, &height, &bytes);
		buffer_size = bytes * width * height;
	}
	else
	{
		gegl_buffer = gimp_drawable_get_buffer (drawable);

		width  = gimp_drawable_width  (drawable);
		height = gimp_drawable_height (drawable);

		if (gimp_drawable_has_alpha (drawable))
			format = babl_format ("R'G'B'A u8");
		else
			format = babl_format ("R'G'B' u8");

		bytes = babl_format_get_bytes_per_pixel (format);

		/* setup image buffer */
		buffer_size = bytes * width * height;
		buffer = g_new (guchar, buffer_size);

		gegl_buffer_get (gegl_buffer, GEGL_RECTANGLE (0, 0, width, height), 1.0,
		                 format, buffer,
		                 GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
		g_object_unref (gegl_buffer);
	}

	check_config (width, height);
	globals.steps[LEFT] = globals.steps[RIGHT] = globals.steps[UP]
	                                                     = globals.steps[DOWN] = (config.x < config.y) ?
	                                                                             (width / config.x * 2) : (height / config.y * 2);

	malloc_cache ();
	draw_jigsaw (buffer, buffer_size, width, height, bytes, preview != NULL);
	free_cache ();

	/* cleanup */
	if (preview)
	{
		gimp_preview_draw_buffer (preview, buffer, width * bytes);
	}
	else
	{
		gegl_buffer = gimp_drawable_get_shadow_buffer (drawable);

		gegl_buffer_set (gegl_buffer, GEGL_RECTANGLE (0, 0, width, height), 0,
		                 format, buffer,
		                 GEGL_AUTO_ROWSTRIDE);
		g_object_unref (gegl_buffer);

		gimp_drawable_merge_shadow (drawable, TRUE);
		gimp_drawable_update (drawable, 0, 0, width, height);
	}

	g_free (buffer);
}

static void
jigsaw_preview (GimpDrawable *drawable,
                GimpPreview  *preview)
{
	jigsaw (drawable, preview);
}

static void
generate_bezier (gint px[4],
                 gint py[4],
                 gint steps,
                 gint *cachex,
                 gint *cachey)
{
	gdouble t = 0.0;
	gdouble sigma = 1.0 / steps;
	gint i;

	for (i = 0; i < steps; i++)
	{
		gdouble t2, t3, x, y, t_1;

		t += sigma;
		t2 = t * t;
		t3 = t2 * t;
		t_1 = 1 - t;
		x = t_1 * t_1 * t_1 * px[0]
		    + 3 * t * t_1 * t_1 * px[1]
		    + 3 * t2 * t_1 * px[2]
		    + t3 * px[3];
		y = t_1 * t_1 * t_1 * py[0]
		    + 3 * t * t_1 * t_1 * py[1]
		    + 3 * t2 * t_1 * py[2]
		    + t3 * py[3];
		cachex[i] = (gint) (x + 0.2);
		cachey[i] = (gint) (y + 0.2);
	} /* for */
}

static void
draw_jigsaw (guchar   *buffer,
             gint bufsize,
             gint width,
             gint height,
             gint bytes,
             gboolean preview_mode)
{
	gint i;
	gint *x, *y;
	gint xtiles = config.x;
	gint ytiles = config.y;
	gint xlines = xtiles - 1;
	gint ylines = ytiles - 1;
	gint blend_lines = config.blend_lines;
	gdouble blend_amount = config.blend_amount;
	gint steps = globals.steps[RIGHT];
	style_t style = config.style;
	gint progress_total = xlines + ylines - 1;

	g_return_if_fail (buffer != NULL);

	globals.gridx = g_new (gint, xtiles);
	globals.gridy = g_new (gint, ytiles);
	x = globals.gridx;
	y = globals.gridy;

	generate_grid (width, height, xtiles, ytiles, globals.gridx, globals.gridy);

	init_right_bump (width, height);
	init_left_bump  (width, height);
	init_up_bump    (width, height);
	init_down_bump  (width, height);

	if (style == BEZIER_1)
	{
		for (i = 0; i < xlines; i++)
		{
			draw_vertical_border (buffer, bufsize, width, height, bytes,
			                      x[i], ytiles,
			                      blend_lines, blend_amount);
			if (!preview_mode)
				gimp_progress_update ((gdouble) i / (gdouble) progress_total);
		}
		for (i = 0; i < ylines; i++)
		{
			draw_horizontal_border (buffer, bufsize, width, bytes, y[i], xtiles,
			                        blend_lines, blend_amount);
			if (!preview_mode)
				gimp_progress_update ((gdouble) (i + xlines) / (gdouble) progress_total);
		}
	}
	else if (style == BEZIER_2)
	{
		for (i = 0; i < xlines; i++)
		{
			draw_bezier_vertical_border (buffer, bufsize, width, height, bytes,
			                             x[i], xtiles, ytiles, blend_lines,
			                             blend_amount, steps);
			if (!preview_mode)
				gimp_progress_update ((gdouble) i / (gdouble) progress_total);
		}
		for (i = 0; i < ylines; i++)
		{
			draw_bezier_horizontal_border (buffer, bufsize, width, height, bytes,
			                               y[i], xtiles, ytiles, blend_lines,
			                               blend_amount, steps);
			if (!preview_mode)
				gimp_progress_update ((gdouble) (i + xlines) / (gdouble) progress_total);
		}
	}
	else
	{
		g_printerr ("draw_jigsaw: bad style\n");
		gimp_quit ();
	}
	gimp_progress_update (1.0);

	g_free (globals.gridx);
	g_free (globals.gridy);
}

static void
draw_vertical_border (guchar  *buffer,
                      gint bufsize,
                      gint width,
                      gint height,
                      gint bytes,
                      gint x_offset,
                      gint ytiles,
                      gint blend_lines,
                      gdouble blend_amount)
{
	gint i, j;
	gint tile_height = height / ytiles;
	gint tile_height_eighth = tile_height / 8;
	gint curve_start_offset = 3 * tile_height_eighth;
	gint curve_end_offset = curve_start_offset + 2 * tile_height_eighth;
	gint px[2], py[2];
	gint ly[2], dy[2];
	gint y_offset = 0;
	gdouble delta;
	gdouble sigma = blend_amount / blend_lines;
	gint right;

	for (i = 0; i < ytiles; i++)
	{
		right = g_random_int_range (0, 2);

		/* first straight line from top downwards */
		px[0] = px[1] = x_offset;
		py[0] = y_offset;
		py[1] = y_offset + curve_start_offset - 1;
		draw_vertical_line (buffer, bufsize, width, bytes, px, py);
		delta = blend_amount;
		dy[0] = ly[0] = py[0];
		dy[1] = ly[1] = py[1];
		if (!right)
		{
			ly[1] += blend_lines + 2;
		}
		for (j = 0; j < blend_lines; j++)
		{
			dy[0]++;
			dy[1]--;
			ly[0]++;
			ly[1]--;
			px[0] = x_offset - j - 1;
			darken_vertical_line (buffer, bufsize, width, bytes, px, dy, delta);
			px[0] = x_offset + j + 1;
			lighten_vertical_line (buffer, bufsize, width, bytes, px, ly, delta);
			delta -= sigma;
		}

		/* top half of curve */
		if (right)
		{
			draw_right_bump (buffer, bufsize, width, bytes, x_offset,
			                 y_offset + curve_start_offset,
			                 globals.steps[RIGHT]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be -j -1 */
				darken_right_bump (buffer, bufsize, width, bytes, x_offset,
				                   y_offset + curve_start_offset,
				                   globals.steps[RIGHT], delta, j);
				/* use to be +j + 1 */
				lighten_right_bump (buffer, bufsize, width, bytes, x_offset,
				                    y_offset + curve_start_offset,
				                    globals.steps[RIGHT], delta, j);
				delta -= sigma;
			}
		}
		else
		{
			draw_left_bump (buffer, bufsize, width, bytes, x_offset,
			                y_offset + curve_start_offset,
			                globals.steps[LEFT]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be -j -1 */
				darken_left_bump (buffer, bufsize, width, bytes, x_offset,
				                  y_offset + curve_start_offset,
				                  globals.steps[LEFT], delta, j);
				/* use to be -j - 1 */
				lighten_left_bump (buffer, bufsize, width, bytes, x_offset,
				                   y_offset + curve_start_offset,
				                   globals.steps[LEFT], delta, j);
				delta -= sigma;
			}
		}
		/* bottom straight line of a tile wall */
		px[0] = px[1] = x_offset;
		py[0] = y_offset + curve_end_offset;
		py[1] = globals.gridy[i];
		draw_vertical_line (buffer, bufsize, width, bytes, px, py);
		delta = blend_amount;
		dy[0] = ly[0] = py[0];
		dy[1] = ly[1] = py[1];
		if (right)
		{
			dy[0] -= blend_lines + 2;
		}
		for (j = 0; j < blend_lines; j++)
		{
			dy[0]++;
			dy[1]--;
			ly[0]++;
			ly[1]--;
			px[0] = x_offset - j - 1;
			darken_vertical_line (buffer, bufsize, width, bytes, px, dy, delta);
			px[0] = x_offset + j + 1;
			lighten_vertical_line (buffer, bufsize, width, bytes, px, ly, delta);
			delta -= sigma;
		}

		y_offset = globals.gridy[i];
	} /* for */
}

/* assumes RGB* */
static void
draw_horizontal_border (guchar   *buffer,
                        gint bufsize,
                        gint width,
                        gint bytes,
                        gint y_offset,
                        gint xtiles,
                        gint blend_lines,
                        gdouble blend_amount)
{
	gint i, j;
	gint tile_width = width / xtiles;
	gint tile_width_eighth = tile_width / 8;
	gint curve_start_offset = 3 * tile_width_eighth;
	gint curve_end_offset = curve_start_offset + 2 * tile_width_eighth;
	gint px[2], py[2];
	gint dx[2], lx[2];
	gint x_offset = 0;
	gdouble delta;
	gdouble sigma = blend_amount / blend_lines;
	gint up;

	for (i = 0; i < xtiles; i++)
	{
		up = g_random_int_range (0, 2);

		/* first horizontal line across */
		px[0] = x_offset;
		px[1] = x_offset + curve_start_offset - 1;
		py[0] = py[1] = y_offset;
		draw_horizontal_line (buffer, bufsize, width, bytes, px, py);
		delta = blend_amount;
		dx[0] = lx[0] = px[0];
		dx[1] = lx[1] = px[1];
		if (up)
		{
			lx[1] += blend_lines + 2;
		}
		for (j = 0; j < blend_lines; j++)
		{
			dx[0]++;
			dx[1]--;
			lx[0]++;
			lx[1]--;
			py[0] = y_offset - j - 1;
			darken_horizontal_line (buffer, bufsize, width, bytes, dx, py,
			                        delta);
			py[0] = y_offset + j + 1;
			lighten_horizontal_line (buffer, bufsize, width, bytes, lx, py,
			                         delta);
			delta -= sigma;
		}

		if (up)
		{
			draw_up_bump (buffer, bufsize, width, bytes, y_offset,
			              x_offset + curve_start_offset,
			              globals.steps[UP]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be -j -1 */
				darken_up_bump (buffer, bufsize, width, bytes, y_offset,
				                x_offset + curve_start_offset,
				                globals.steps[UP], delta, j);
				/* use to be +j + 1 */
				lighten_up_bump (buffer, bufsize, width, bytes, y_offset,
				                 x_offset + curve_start_offset,
				                 globals.steps[UP], delta, j);
				delta -= sigma;
			}
		}
		else
		{
			draw_down_bump (buffer, bufsize, width, bytes, y_offset,
			                x_offset + curve_start_offset,
			                globals.steps[DOWN]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be +j + 1 */
				darken_down_bump (buffer, bufsize, width, bytes, y_offset,
				                  x_offset + curve_start_offset,
				                  globals.steps[DOWN], delta, j);
				/* use to be -j -1 */
				lighten_down_bump (buffer, bufsize, width, bytes, y_offset,
				                   x_offset + curve_start_offset,
				                   globals.steps[DOWN], delta, j);
				delta -= sigma;
			}
		}
		/* right horizontal line of tile */
		px[0] = x_offset + curve_end_offset;
		px[1] = globals.gridx[i];
		py[0] = py[1] = y_offset;
		draw_horizontal_line (buffer, bufsize, width, bytes, px, py);
		delta = blend_amount;
		dx[0] = lx[0] = px[0];
		dx[1] = lx[1] = px[1];
		if (!up)
		{
			dx[0] -= blend_lines + 2;
		}
		for (j = 0; j < blend_lines; j++)
		{
			dx[0]++;
			dx[1]--;
			lx[0]++;
			lx[1]--;
			py[0] = y_offset - j - 1;
			darken_horizontal_line (buffer, bufsize, width, bytes, dx, py,
			                        delta);
			py[0] = y_offset + j + 1;
			lighten_horizontal_line (buffer, bufsize, width, bytes, lx, py,
			                         delta);
			delta -= sigma;
		}
		x_offset = globals.gridx[i];
	} /* for */
}

/* assumes going top to bottom */
static void
draw_vertical_line (guchar   *buffer,
                    gint bufsize,
                    gint width,
                    gint bytes,
                    gint px[2],
                    gint py[2])
{
	gint i;
	gint rowstride;
	gint index;
	gint length;

	rowstride = bytes * width;
	index = px[0] * bytes + rowstride * py[0];
	length = py[1] - py[0] + 1;

	for (i = 0; i < length; i++)
	{
		DRAW_POINT (buffer, bufsize, index);
		index += rowstride;
	}
}

/* assumes going left to right */
static void
draw_horizontal_line (guchar   *buffer,
                      gint bufsize,
                      gint width,
                      gint bytes,
                      gint px[2],
                      gint py[2])
{
	gint i;
	gint rowstride;
	gint index;
	gint length;

	rowstride = bytes * width;
	index = px[0] * bytes + rowstride * py[0];
	length = px[1] - px[0] + 1;

	for (i = 0; i < length; i++)
	{
		DRAW_POINT (buffer, bufsize, index);
		index += bytes;
	}
}

static void
draw_right_bump (guchar   *buffer,
                 gint bufsize,
                 gint width,
                 gint bytes,
                 gint x_offset,
                 gint curve_start_offset,
                 gint steps)
{
	gint i;
	gint x, y;
	gint index;
	gint rowstride;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = globals.cachex1[RIGHT][i] + x_offset;
		y = globals.cachey1[RIGHT][i] + curve_start_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);

		x = globals.cachex2[RIGHT][i] + x_offset;
		y = globals.cachey2[RIGHT][i] + curve_start_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);
	}
}

static void
draw_left_bump (guchar   *buffer,
                gint bufsize,
                gint width,
                gint bytes,
                gint x_offset,
                gint curve_start_offset,
                gint steps)
{
	gint i;
	gint x, y;
	gint index;
	gint rowstride;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = globals.cachex1[LEFT][i] + x_offset;
		y = globals.cachey1[LEFT][i] + curve_start_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);

		x = globals.cachex2[LEFT][i] + x_offset;
		y = globals.cachey2[LEFT][i] + curve_start_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);
	}
}

static void
draw_up_bump (guchar   *buffer,
              gint bufsize,
              gint width,
              gint bytes,
              gint y_offset,
              gint curve_start_offset,
              gint steps)
{
	gint i;
	gint x, y;
	gint index;
	gint rowstride;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = globals.cachex1[UP][i] + curve_start_offset;
		y = globals.cachey1[UP][i] + y_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);

		x = globals.cachex2[UP][i] + curve_start_offset;
		y = globals.cachey2[UP][i] + y_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);
	}
}

static void
draw_down_bump (guchar   *buffer,
                gint bufsize,
                gint width,
                gint bytes,
                gint y_offset,
                gint curve_start_offset,
                gint steps)
{
	gint i;
	gint x, y;
	gint index;
	gint rowstride;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = globals.cachex1[DOWN][i] + curve_start_offset;
		y = globals.cachey1[DOWN][i] + y_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);

		x = globals.cachex2[DOWN][i] + curve_start_offset;
		y = globals.cachey2[DOWN][i] + y_offset;
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);
	}
}

static void
malloc_cache (void)
{
	gint i, j;
	gint blend_lines = config.blend_lines;

	for (i = 0; i < 4; i++)
	{
		gint steps = globals.steps[i];

		globals.cachex1[i] = g_new (gint, steps);
		globals.cachex2[i] = g_new (gint, steps);
		globals.cachey1[i] = g_new (gint, steps);
		globals.cachey2[i] = g_new (gint, steps);
		globals.blend_outer_cachex1[i] = g_new (gint *, blend_lines);
		globals.blend_outer_cachex2[i] = g_new (gint *, blend_lines);
		globals.blend_outer_cachey1[i] = g_new (gint *, blend_lines);
		globals.blend_outer_cachey2[i] = g_new (gint *, blend_lines);
		globals.blend_inner_cachex1[i] = g_new (gint *, blend_lines);
		globals.blend_inner_cachex2[i] = g_new (gint *, blend_lines);
		globals.blend_inner_cachey1[i] = g_new (gint *, blend_lines);
		globals.blend_inner_cachey2[i] = g_new (gint *, blend_lines);
		for (j = 0; j < blend_lines; j++)
		{
			globals.blend_outer_cachex1[i][j] = g_new (gint, steps);
			globals.blend_outer_cachex2[i][j] = g_new (gint, steps);
			globals.blend_outer_cachey1[i][j] = g_new (gint, steps);
			globals.blend_outer_cachey2[i][j] = g_new (gint, steps);
			globals.blend_inner_cachex1[i][j] = g_new (gint, steps);
			globals.blend_inner_cachex2[i][j] = g_new (gint, steps);
			globals.blend_inner_cachey1[i][j] = g_new (gint, steps);
			globals.blend_inner_cachey2[i][j] = g_new (gint, steps);
		}
	}
}

static void
free_cache (void)
{
	gint i, j;
	gint blend_lines = config.blend_lines;

	for (i = 0; i < 4; i++)
	{
		g_free (globals.cachex1[i]);
		g_free (globals.cachex2[i]);
		g_free (globals.cachey1[i]);
		g_free (globals.cachey2[i]);
		for (j = 0; j < blend_lines; j++)
		{
			g_free (globals.blend_outer_cachex1[i][j]);
			g_free (globals.blend_outer_cachex2[i][j]);
			g_free (globals.blend_outer_cachey1[i][j]);
			g_free (globals.blend_outer_cachey2[i][j]);
			g_free (globals.blend_inner_cachex1[i][j]);
			g_free (globals.blend_inner_cachex2[i][j]);
			g_free (globals.blend_inner_cachey1[i][j]);
			g_free (globals.blend_inner_cachey2[i][j]);
		}
		g_free (globals.blend_outer_cachex1[i]);
		g_free (globals.blend_outer_cachex2[i]);
		g_free (globals.blend_outer_cachey1[i]);
		g_free (globals.blend_outer_cachey2[i]);
		g_free (globals.blend_inner_cachex1[i]);
		g_free (globals.blend_inner_cachex2[i]);
		g_free (globals.blend_inner_cachey1[i]);
		g_free (globals.blend_inner_cachey2[i]);
	}
}

static void
init_right_bump (gint width,
                 gint height)
{
	gint i;
	gint xtiles = config.x;
	gint ytiles = config.y;
	gint steps = globals.steps[RIGHT];
	gint px[4], py[4];
	gint x_offset = 0;
	gint tile_width =  width / xtiles;
	gint tile_height = height/ ytiles;
	gint tile_height_eighth = tile_height / 8;
	gint curve_start_offset = 0;
	gint curve_end_offset = curve_start_offset + 2 * tile_height_eighth;
	gint blend_lines = config.blend_lines;

	px[0] = x_offset;
	px[1] = x_offset + XFACTOR2 * tile_width;
	px[2] = x_offset + XFACTOR3 * tile_width;
	px[3] = x_offset + XFACTOR4 * tile_width;
	py[0] = curve_start_offset;
	py[1] = curve_start_offset + YFACTOR2 * tile_height;
	py[2] = curve_start_offset - YFACTOR3 * tile_height;
	py[3] = curve_start_offset + YFACTOR4 * tile_height;
	generate_bezier(px, py, steps, globals.cachex1[RIGHT],
	                globals.cachey1[RIGHT]);
	/* outside right bump */
	for (i = 0; i < blend_lines; i++)
	{
		py[0]--;
		py[1]--;
		py[2]--;
		px[3]++;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex1[RIGHT][i],
		                globals.blend_outer_cachey1[RIGHT][i]);
	}
	/* inside right bump */
	py[0] += blend_lines;
	py[1] += blend_lines;
	py[2] += blend_lines;
	px[3] -= blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		py[0]++;
		py[1]++;
		py[2]++;
		px[3]--;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex1[RIGHT][i],
		                globals.blend_inner_cachey1[RIGHT][i]);
	}

	/* bottom half of bump */
	px[0] = x_offset + XFACTOR5 * tile_width;
	px[1] = x_offset + XFACTOR6 * tile_width;
	px[2] = x_offset + XFACTOR7 * tile_width;
	px[3] = x_offset;
	py[0] = curve_start_offset + YFACTOR5 * tile_height;
	py[1] = curve_start_offset + YFACTOR6 * tile_height;
	py[2] = curve_start_offset + YFACTOR7 * tile_height;
	py[3] = curve_end_offset;
	generate_bezier(px, py, steps, globals.cachex2[RIGHT],
	                globals.cachey2[RIGHT]);
	/* outer right bump */
	for (i = 0; i < blend_lines; i++)
	{
		py[1]++;
		py[2]++;
		py[3]++;
		px[0]++;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex2[RIGHT][i],
		                globals.blend_outer_cachey2[RIGHT][i]);
	}
	/* inner right bump */
	py[1] -= blend_lines;
	py[2] -= blend_lines;
	py[3] -= blend_lines;
	px[0] -= blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		py[1]--;
		py[2]--;
		py[3]--;
		px[0]--;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex2[RIGHT][i],
		                globals.blend_inner_cachey2[RIGHT][i]);
	}
}

static void
init_left_bump (gint width,
                gint height)
{
	gint i;
	gint xtiles = config.x;
	gint ytiles = config.y;
	gint steps = globals.steps[LEFT];
	gint px[4], py[4];
	gint x_offset = 0;
	gint tile_width = width / xtiles;
	gint tile_height = height / ytiles;
	gint tile_height_eighth = tile_height / 8;
	gint curve_start_offset = 0;
	gint curve_end_offset = curve_start_offset + 2 * tile_height_eighth;
	gint blend_lines = config.blend_lines;

	px[0] = x_offset;
	px[1] = x_offset - XFACTOR2 * tile_width;
	px[2] = x_offset - XFACTOR3 * tile_width;
	px[3] = x_offset - XFACTOR4 * tile_width;
	py[0] = curve_start_offset;
	py[1] = curve_start_offset + YFACTOR2 * tile_height;
	py[2] = curve_start_offset - YFACTOR3 * tile_height;
	py[3] = curve_start_offset + YFACTOR4 * tile_height;
	generate_bezier(px, py, steps, globals.cachex1[LEFT],
	                globals.cachey1[LEFT]);
	/* outer left bump */
	for (i = 0; i < blend_lines; i++)
	{
		py[0]--;
		py[1]--;
		py[2]--;
		px[3]--;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex1[LEFT][i],
		                globals.blend_outer_cachey1[LEFT][i]);
	}
	/* inner left bump */
	py[0] += blend_lines;
	py[1] += blend_lines;
	py[2] += blend_lines;
	px[3] += blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		py[0]++;
		py[1]++;
		py[2]++;
		px[3]++;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex1[LEFT][i],
		                globals.blend_inner_cachey1[LEFT][i]);
	}

	/* bottom half of bump */
	px[0] = x_offset - XFACTOR5 * tile_width;
	px[1] = x_offset - XFACTOR6 * tile_width;
	px[2] = x_offset - XFACTOR7 * tile_width;
	px[3] = x_offset;
	py[0] = curve_start_offset + YFACTOR5 * tile_height;
	py[1] = curve_start_offset + YFACTOR6 * tile_height;
	py[2] = curve_start_offset + YFACTOR7 * tile_height;
	py[3] = curve_end_offset;
	generate_bezier(px, py, steps, globals.cachex2[LEFT],
	                globals.cachey2[LEFT]);
	/* outer left bump */
	for (i = 0; i < blend_lines; i++)
	{
		py[1]++;
		py[2]++;
		py[3]++;
		px[0]--;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex2[LEFT][i],
		                globals.blend_outer_cachey2[LEFT][i]);
	}
	/* inner left bump */
	py[1] -= blend_lines;
	py[2] -= blend_lines;
	py[3] -= blend_lines;
	px[0] += blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		py[1]--;
		py[2]--;
		py[3]--;
		px[0]++;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex2[LEFT][i],
		                globals.blend_inner_cachey2[LEFT][i]);
	}
}

static void
init_up_bump (gint width,
              gint height)
{
	gint i;
	gint xtiles = config.x;
	gint ytiles = config.y;
	gint steps = globals.steps[UP];
	gint px[4], py[4];
	gint y_offset = 0;
	gint tile_width =  width / xtiles;
	gint tile_height = height/ ytiles;
	gint tile_width_eighth = tile_width / 8;
	gint curve_start_offset = 0;
	gint curve_end_offset = curve_start_offset + 2 * tile_width_eighth;
	gint blend_lines = config.blend_lines;

	px[0] = curve_start_offset;
	px[1] = curve_start_offset + YFACTOR2 * tile_width;
	px[2] = curve_start_offset - YFACTOR3 * tile_width;
	px[3] = curve_start_offset + YFACTOR4 * tile_width;
	py[0] = y_offset;
	py[1] = y_offset - XFACTOR2 * tile_height;
	py[2] = y_offset - XFACTOR3 * tile_height;
	py[3] = y_offset - XFACTOR4 * tile_height;
	generate_bezier(px, py, steps, globals.cachex1[UP],
	                globals.cachey1[UP]);
	/* outer up bump */
	for (i = 0; i < blend_lines; i++)
	{
		px[0]--;
		px[1]--;
		px[2]--;
		py[3]--;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex1[UP][i],
		                globals.blend_outer_cachey1[UP][i]);
	}
	/* inner up bump */
	px[0] += blend_lines;
	px[1] += blend_lines;
	px[2] += blend_lines;
	py[3] += blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		px[0]++;
		px[1]++;
		px[2]++;
		py[3]++;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex1[UP][i],
		                globals.blend_inner_cachey1[UP][i]);
	}

	/* bottom half of bump */
	px[0] = curve_start_offset + YFACTOR5 * tile_width;
	px[1] = curve_start_offset + YFACTOR6 * tile_width;
	px[2] = curve_start_offset + YFACTOR7 * tile_width;
	px[3] = curve_end_offset;
	py[0] = y_offset - XFACTOR5 * tile_height;
	py[1] = y_offset - XFACTOR6 * tile_height;
	py[2] = y_offset - XFACTOR7 * tile_height;
	py[3] = y_offset;
	generate_bezier(px, py, steps, globals.cachex2[UP],
	                globals.cachey2[UP]);
	/* outer up bump */
	for (i = 0; i < blend_lines; i++)
	{
		px[1]++;
		px[2]++;
		px[3]++;
		py[0]--;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex2[UP][i],
		                globals.blend_outer_cachey2[UP][i]);
	}
	/* inner up bump */
	px[1] -= blend_lines;
	px[2] -= blend_lines;
	px[3] -= blend_lines;
	py[0] += blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		px[1]--;
		px[2]--;
		px[3]--;
		py[0]++;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex2[UP][i],
		                globals.blend_inner_cachey2[UP][i]);
	}
}

static void
init_down_bump (gint width,
                gint height)
{
	gint i;
	gint xtiles = config.x;
	gint ytiles = config.y;
	gint steps = globals.steps[DOWN];
	gint px[4], py[4];
	gint y_offset = 0;
	gint tile_width =  width / xtiles;
	gint tile_height = height/ ytiles;
	gint tile_width_eighth = tile_width / 8;
	gint curve_start_offset = 0;
	gint curve_end_offset = curve_start_offset + 2 * tile_width_eighth;
	gint blend_lines = config.blend_lines;

	px[0] = curve_start_offset;
	px[1] = curve_start_offset + YFACTOR2 * tile_width;
	px[2] = curve_start_offset - YFACTOR3 * tile_width;
	px[3] = curve_start_offset + YFACTOR4 * tile_width;
	py[0] = y_offset;
	py[1] = y_offset + XFACTOR2 * tile_height;
	py[2] = y_offset + XFACTOR3 * tile_height;
	py[3] = y_offset + XFACTOR4 * tile_height;
	generate_bezier(px, py, steps, globals.cachex1[DOWN],
	                globals.cachey1[DOWN]);
	/* outer down bump */
	for (i = 0; i < blend_lines; i++)
	{
		px[0]--;
		px[1]--;
		px[2]--;
		py[3]++;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex1[DOWN][i],
		                globals.blend_outer_cachey1[DOWN][i]);
	}
	/* inner down bump */
	px[0] += blend_lines;
	px[1] += blend_lines;
	px[2] += blend_lines;
	py[3] -= blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		px[0]++;
		px[1]++;
		px[2]++;
		py[3]--;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex1[DOWN][i],
		                globals.blend_inner_cachey1[DOWN][i]);
	}

	/* bottom half of bump */
	px[0] = curve_start_offset + YFACTOR5 * tile_width;
	px[1] = curve_start_offset + YFACTOR6 * tile_width;
	px[2] = curve_start_offset + YFACTOR7 * tile_width;
	px[3] = curve_end_offset;
	py[0] = y_offset + XFACTOR5 * tile_height;
	py[1] = y_offset + XFACTOR6 * tile_height;
	py[2] = y_offset + XFACTOR7 * tile_height;
	py[3] = y_offset;
	generate_bezier(px, py, steps, globals.cachex2[DOWN],
	                globals.cachey2[DOWN]);
	/* outer down bump */
	for (i = 0; i < blend_lines; i++)
	{
		px[1]++;
		px[2]++;
		px[3]++;
		py[0]++;
		generate_bezier(px, py, steps,
		                globals.blend_outer_cachex2[DOWN][i],
		                globals.blend_outer_cachey2[DOWN][i]);
	}
	/* inner down bump */
	px[1] -= blend_lines;
	px[2] -= blend_lines;
	px[3] -= blend_lines;
	py[0] -= blend_lines;
	for (i = 0; i < blend_lines; i++)
	{
		px[1]--;
		px[2]--;
		px[3]--;
		py[0]--;
		generate_bezier(px, py, steps,
		                globals.blend_inner_cachex2[DOWN][i],
		                globals.blend_inner_cachey2[DOWN][i]);
	}
}

static void
generate_grid (gint width,
               gint height,
               gint xtiles,
               gint ytiles,
               gint *x,
               gint *y)
{
	gint i;
	gint xlines = xtiles - 1;
	gint ylines = ytiles - 1;
	gint tile_width = width / xtiles;
	gint tile_height = height / ytiles;
	gint tile_width_leftover = width % xtiles;
	gint tile_height_leftover = height % ytiles;
	gint x_offset = tile_width;
	gint y_offset = tile_height;
	gint carry;

	for (i = 0; i < xlines; i++)
	{
		x[i] = x_offset;
		x_offset += tile_width;
	}
	carry = 0;
	while (tile_width_leftover)
	{
		for (i = carry; i < xlines; i++)
		{
			x[i] += 1;
		}
		tile_width_leftover--;
		carry++;
	}
	x[xlines] = width - 1; /* padding for draw_horizontal_border */

	for (i = 0; i < ytiles; i++)
	{
		y[i] = y_offset;
		y_offset += tile_height;
	}
	carry = 0;
	while (tile_height_leftover)
	{
		for (i = carry; i < ylines; i++)
		{
			y[i] += 1;
		}
		tile_height_leftover--;
		carry++;
	}
	y[ylines] = height - 1; /* padding for draw_vertical_border */
}

/* assumes RGB* */
/* assumes py[1] > py[0] and px[0] = px[1] */
static void
darken_vertical_line (guchar   *buffer,
                      gint bufsize,
                      gint width,
                      gint bytes,
                      gint px[2],
                      gint py[2],
                      gdouble delta)
{
	gint i;
	gint rowstride;
	gint index;
	gint length;
	gint temp;

	rowstride = bytes * width;
	index = px[0] * bytes + py[0] * rowstride;
	length = py[1] - py[0] + 1;

	for (i = 0; i < length; i++)
	{
		DARKEN_POINT (buffer, bufsize, index, delta, temp);
		index += rowstride;
	}
}

/* assumes RGB* */
/* assumes py[1] > py[0] and px[0] = px[1] */
static void
lighten_vertical_line (guchar   *buffer,
                       gint bufsize,
                       gint width,
                       gint bytes,
                       gint px[2],
                       gint py[2],
                       gdouble delta)
{
	gint i;
	gint rowstride;
	gint index;
	gint length;
	gint temp;

	rowstride = bytes * width;
	index = px[0] * bytes + py[0] * rowstride;
	length = py[1] - py[0] + 1;

	for (i = 0; i < length; i++)
	{
		LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
		index += rowstride;
	}
}

/* assumes RGB* */
/* assumes px[1] > px[0] and py[0] = py[1] */
static void
darken_horizontal_line (guchar   *buffer,
                        gint bufsize,
                        gint width,
                        gint bytes,
                        gint px[2],
                        gint py[2],
                        gdouble delta)
{
	gint i;
	gint rowstride;
	gint index;
	gint length;
	gint temp;

	rowstride = bytes * width;
	index = px[0] * bytes + py[0] * rowstride;
	length = px[1] - px[0] + 1;

	for (i = 0; i < length; i++)
	{
		DARKEN_POINT (buffer, bufsize, index, delta, temp);
		index += bytes;
	}
}

/* assumes RGB* */
/* assumes px[1] > px[0] and py[0] = py[1] */
static void
lighten_horizontal_line (guchar   *buffer,
                         gint bufsize,
                         gint width,
                         gint bytes,
                         gint px[2],
                         gint py[2],
                         gdouble delta)
{
	gint i;
	gint rowstride;
	gint index;
	gint length;
	gint temp;

	rowstride = bytes * width;
	index = px[0] * bytes + py[0] * rowstride;
	length = px[1] - px[0] + 1;

	for (i = 0; i < length; i++)
	{
		LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
		index += bytes;
	}
}

static void
darken_right_bump (guchar *buffer,
                   gint bufsize,
                   gint width,
                   gint bytes,
                   gint x_offset,
                   gint curve_start_offset,
                   gint steps,
                   gdouble delta,
                   gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = x_offset
		    + globals.blend_inner_cachex1[RIGHT][j][i];
		y = curve_start_offset
		    + globals.blend_inner_cachey1[RIGHT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			if (i < steps / 1.3)
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index1 = index;
		}

		x = x_offset
		    + globals.blend_inner_cachex2[RIGHT][j][i];
		y = curve_start_offset
		    + globals.blend_inner_cachey2[RIGHT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			DARKEN_POINT (buffer, bufsize, index, delta, temp);
			last_index2 = index;
		}
	}
}

static void
lighten_right_bump (guchar   *buffer,
                    gint bufsize,
                    gint width,
                    gint bytes,
                    gint x_offset,
                    gint curve_start_offset,
                    gint steps,
                    gdouble delta,
                    gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = x_offset
		    + globals.blend_outer_cachex1[RIGHT][j][i];
		y = curve_start_offset
		    + globals.blend_outer_cachey1[RIGHT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			if (i < steps / 1.3)
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index1 = index;
		}

		x = x_offset
		    + globals.blend_outer_cachex2[RIGHT][j][i];
		y = curve_start_offset
		    + globals.blend_outer_cachey2[RIGHT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			last_index2 = index;
		}
	}
}

static void
darken_left_bump (guchar   *buffer,
                  gint bufsize,
                  gint width,
                  gint bytes,
                  gint x_offset,
                  gint curve_start_offset,
                  gint steps,
                  gdouble delta,
                  gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = x_offset
		    + globals.blend_outer_cachex1[LEFT][j][i];
		y = curve_start_offset
		    + globals.blend_outer_cachey1[LEFT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			DARKEN_POINT (buffer, bufsize, index, delta, temp);
			last_index1 = index;
		}

		x = x_offset
		    + globals.blend_outer_cachex2[LEFT][j][i];
		y = curve_start_offset
		    + globals.blend_outer_cachey2[LEFT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			if (i < steps / 4)
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index2 = index;
		}
	}
}

static void
lighten_left_bump (guchar *buffer,
                   gint bufsize,
                   gint width,
                   gint bytes,
                   gint x_offset,
                   gint curve_start_offset,
                   gint steps,
                   gdouble delta,
                   gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = x_offset
		    + globals.blend_inner_cachex1[LEFT][j][i];
		y = curve_start_offset
		    + globals.blend_inner_cachey1[LEFT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			last_index1 = index;
		}

		x = x_offset
		    + globals.blend_inner_cachex2[LEFT][j][i];
		y = curve_start_offset
		    + globals.blend_inner_cachey2[LEFT][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			if (i < steps / 4)
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index2 = index;
		}
	}
}

static void
darken_up_bump (guchar   *buffer,
                gint bufsize,
                gint width,
                gint bytes,
                gint y_offset,
                gint curve_start_offset,
                gint steps,
                gdouble delta,
                gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = curve_start_offset
		    + globals.blend_outer_cachex1[UP][j][i];
		y = y_offset
		    + globals.blend_outer_cachey1[UP][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			DARKEN_POINT (buffer, bufsize, index, delta, temp);
			last_index1 = index;
		}

		x = curve_start_offset
		    + globals.blend_outer_cachex2[UP][j][i];
		y = y_offset
		    + globals.blend_outer_cachey2[UP][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			if (i < steps / 4)
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index2 = index;
		}
	}
}

static void
lighten_up_bump (guchar   *buffer,
                 gint bufsize,
                 gint width,
                 gint bytes,
                 gint y_offset,
                 gint curve_start_offset,
                 gint steps,
                 gdouble delta,
                 gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = curve_start_offset
		    + globals.blend_inner_cachex1[UP][j][i];
		y = y_offset
		    + globals.blend_inner_cachey1[UP][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			last_index1 = index;
		}

		x = curve_start_offset
		    + globals.blend_inner_cachex2[UP][j][i];
		y = y_offset
		    + globals.blend_inner_cachey2[UP][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			if (i < steps / 4)
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index2 = index;
		}
	}
}

static void
darken_down_bump (guchar   *buffer,
                  gint bufsize,
                  gint width,
                  gint bytes,
                  gint y_offset,
                  gint curve_start_offset,
                  gint steps,
                  gdouble delta,
                  gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = curve_start_offset
		    + globals.blend_inner_cachex1[DOWN][j][i];
		y = y_offset
		    + globals.blend_inner_cachey1[DOWN][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			if (i < steps / 1.2)
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index1 = index;
		}

		x = curve_start_offset
		    + globals.blend_inner_cachex2[DOWN][j][i];
		y = y_offset
		    + globals.blend_inner_cachey2[DOWN][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			DARKEN_POINT (buffer, bufsize, index, delta, temp);
			last_index2 = index;
		}
	}
}

static void
lighten_down_bump (guchar   *buffer,
                   gint bufsize,
                   gint width,
                   gint bytes,
                   gint y_offset,
                   gint curve_start_offset,
                   gint steps,
                   gdouble delta,
                   gint counter)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index1 = -1;
	gint last_index2 = -1;
	gint rowstride;
	gint temp;
	gint j = counter;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = curve_start_offset
		    + globals.blend_outer_cachex1[DOWN][j][i];
		y = y_offset
		    + globals.blend_outer_cachey1[DOWN][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index1)
		{
			if (i < steps / 1.2)
			{
				DARKEN_POINT (buffer, bufsize, index, delta, temp);
			}
			else
			{
				LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			}
			last_index1 = index;
		}

		x = curve_start_offset
		    + globals.blend_outer_cachex2[DOWN][j][i];
		y = y_offset
		    + globals.blend_outer_cachey2[DOWN][j][i];
		index = y * rowstride + x * bytes;
		if (index != last_index2)
		{
			LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			last_index2 = index;
		}
	}
}

static void
draw_bezier_line (guchar   *buffer,
                  gint bufsize,
                  gint width,
                  gint bytes,
                  gint steps,
                  gint     *cx,
                  gint     *cy)
{
	gint i;
	gint x, y;
	gint index;
	gint rowstride;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = cx[i];
		y = cy[i];
		index = y * rowstride + x * bytes;
		DRAW_POINT (buffer, bufsize, index);
	}
}

static void
darken_bezier_line (guchar   *buffer,
                    gint bufsize,
                    gint width,
                    gint bytes,
                    gint x_offset,
                    gint y_offset,
                    gint steps,
                    gint     *cx,
                    gint     *cy,
                    gdouble delta)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index = -1;
	gint rowstride;
	gint temp;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = cx[i] + x_offset;
		y = cy[i] + y_offset;
		index = y * rowstride + x * bytes;
		if (index != last_index)
		{
			DARKEN_POINT (buffer, bufsize, index, delta, temp);
			last_index = index;
		}
	}
}

static void
lighten_bezier_line (guchar   *buffer,
                     gint bufsize,
                     gint width,
                     gint bytes,
                     gint x_offset,
                     gint y_offset,
                     gint steps,
                     gint     *cx,
                     gint     *cy,
                     gdouble delta)
{
	gint i;
	gint x, y;
	gint index;
	gint last_index = -1;
	gint rowstride;
	gint temp;

	rowstride = bytes * width;

	for (i = 0; i < steps; i++)
	{
		x = cx[i] + x_offset;
		y = cy[i] + y_offset;
		index = y * rowstride + x * bytes;
		if (index != last_index)
		{
			LIGHTEN_POINT (buffer, bufsize, index, delta, temp);
			last_index = index;
		}
	}
}

static void
draw_bezier_vertical_border (guchar   *buffer,
                             gint bufsize,
                             gint width,
                             gint height,
                             gint bytes,
                             gint x_offset,
                             gint xtiles,
                             gint ytiles,
                             gint blend_lines,
                             gdouble blend_amount,
                             gint steps)
{
	gint i, j;
	gint tile_width = width / xtiles;
	gint tile_height = height / ytiles;
	gint tile_height_eighth = tile_height / 8;
	gint curve_start_offset = 3 * tile_height_eighth;
	gint curve_end_offset = curve_start_offset + 2 * tile_height_eighth;
	gint px[4], py[4];
	gint y_offset = 0;
	gdouble delta;
	gdouble sigma = blend_amount / blend_lines;
	gint right;
	gint *cachex, *cachey;

	cachex = g_new (gint, steps);
	cachey = g_new (gint, steps);

	for (i = 0; i < ytiles; i++)
	{
		right = g_random_int_range (0, 2);

		px[0] = px[3] = x_offset;
		px[1] = x_offset + WALL_XFACTOR2 * tile_width * FUDGE;
		px[2] = x_offset + WALL_XFACTOR3 * tile_width * FUDGE;
		py[0] = y_offset;
		py[1] = y_offset + WALL_YCONS2 * tile_height;
		py[2] = y_offset + WALL_YCONS3 * tile_height;
		py[3] = y_offset + curve_start_offset;

		if (right)
		{
			px[1] = x_offset - WALL_XFACTOR2 * tile_width;
			px[2] = x_offset - WALL_XFACTOR3 * tile_width;
		}
		generate_bezier (px, py, steps, cachex, cachey);
		draw_bezier_line (buffer, bufsize, width, bytes, steps, cachex, cachey);
		delta = blend_amount;
		for (j = 0; j < blend_lines; j++)
		{
			px[0] =  -j - 1;
			darken_bezier_line (buffer, bufsize, width, bytes, px[0], 0,
			                    steps, cachex, cachey, delta);
			px[0] =  j + 1;
			lighten_bezier_line (buffer, bufsize, width, bytes, px[0], 0,
			                     steps, cachex, cachey, delta);
			delta -= sigma;
		}
		if (right)
		{
			draw_right_bump (buffer, bufsize, width, bytes, x_offset,
			                 y_offset + curve_start_offset,
			                 globals.steps[RIGHT]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be -j -1 */
				darken_right_bump (buffer, bufsize, width, bytes, x_offset,
				                   y_offset + curve_start_offset,
				                   globals.steps[RIGHT], delta, j);
				/* use to be +j + 1 */
				lighten_right_bump (buffer, bufsize, width, bytes, x_offset,
				                    y_offset + curve_start_offset,
				                    globals.steps[RIGHT], delta, j);
				delta -= sigma;
			}
		}
		else
		{
			draw_left_bump (buffer, bufsize, width, bytes, x_offset,
			                y_offset + curve_start_offset,
			                globals.steps[LEFT]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be -j -1 */
				darken_left_bump (buffer, bufsize, width, bytes, x_offset,
				                  y_offset + curve_start_offset,
				                  globals.steps[LEFT], delta, j);
				/* use to be -j - 1 */
				lighten_left_bump (buffer, bufsize, width, bytes, x_offset,
				                   y_offset + curve_start_offset,
				                   globals.steps[LEFT], delta, j);
				delta -= sigma;
			}
		}
		px[0] = px[3] = x_offset;
		px[1] = x_offset + WALL_XFACTOR2 * tile_width * FUDGE;
		px[2] = x_offset + WALL_XFACTOR3 * tile_width * FUDGE;
		py[0] = y_offset + curve_end_offset;
		py[1] = y_offset + curve_end_offset + WALL_YCONS2 * tile_height;
		py[2] = y_offset + curve_end_offset + WALL_YCONS3 * tile_height;
		py[3] = globals.gridy[i];
		if (right)
		{
			px[1] = x_offset - WALL_XFACTOR2 * tile_width;
			px[2] = x_offset - WALL_XFACTOR3 * tile_width;
		}
		generate_bezier (px, py, steps, cachex, cachey);
		draw_bezier_line (buffer, bufsize, width, bytes, steps, cachex, cachey);
		delta = blend_amount;
		for (j = 0; j < blend_lines; j++)
		{
			px[0] =  -j - 1;
			darken_bezier_line (buffer, bufsize, width, bytes, px[0], 0,
			                    steps, cachex, cachey, delta);
			px[0] =  j + 1;
			lighten_bezier_line (buffer, bufsize, width, bytes, px[0], 0,
			                     steps, cachex, cachey, delta);
			delta -= sigma;
		}
		y_offset = globals.gridy[i];
	} /* for */
	g_free(cachex);
	g_free(cachey);
}

static void
draw_bezier_horizontal_border (guchar   *buffer,
                               gint bufsize,
                               gint width,
                               gint height,
                               gint bytes,
                               gint y_offset,
                               gint xtiles,
                               gint ytiles,
                               gint blend_lines,
                               gdouble blend_amount,
                               gint steps)
{
	gint i, j;
	gint tile_width = width / xtiles;
	gint tile_height = height / ytiles;
	gint tile_width_eighth = tile_width / 8;
	gint curve_start_offset = 3 * tile_width_eighth;
	gint curve_end_offset = curve_start_offset + 2 * tile_width_eighth;
	gint px[4], py[4];
	gint x_offset = 0;
	gdouble delta;
	gdouble sigma = blend_amount / blend_lines;
	gint up;
	gint *cachex, *cachey;

	cachex = g_new (gint, steps);
	cachey = g_new (gint, steps);

	for (i = 0; i < xtiles; i++)
	{
		up = g_random_int_range (0, 2);

		px[0] = x_offset;
		px[1] = x_offset + WALL_XCONS2 * tile_width;
		px[2] = x_offset + WALL_XCONS3 * tile_width;
		px[3] = x_offset + curve_start_offset;
		py[0] = py[3] = y_offset;
		py[1] = y_offset + WALL_YFACTOR2 * tile_height * FUDGE;
		py[2] = y_offset + WALL_YFACTOR3 * tile_height * FUDGE;
		if (!up)
		{
			py[1] = y_offset - WALL_YFACTOR2 * tile_height;
			py[2] = y_offset - WALL_YFACTOR3 * tile_height;
		}
		generate_bezier (px, py, steps, cachex, cachey);
		draw_bezier_line (buffer, bufsize, width, bytes, steps, cachex, cachey);
		delta = blend_amount;
		for (j = 0; j < blend_lines; j++)
		{
			py[0] = -j - 1;
			darken_bezier_line (buffer, bufsize, width, bytes, 0, py[0],
			                    steps, cachex, cachey, delta);
			py[0] = j + 1;
			lighten_bezier_line (buffer, bufsize, width, bytes, 0, py[0],
			                     steps, cachex, cachey, delta);
			delta -= sigma;
		}
		/* bumps */
		if (up)
		{
			draw_up_bump (buffer, bufsize, width, bytes, y_offset,
			              x_offset + curve_start_offset,
			              globals.steps[UP]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be -j -1 */
				darken_up_bump (buffer, bufsize, width, bytes, y_offset,
				                x_offset + curve_start_offset,
				                globals.steps[UP], delta, j);
				/* use to be +j + 1 */
				lighten_up_bump (buffer, bufsize, width, bytes, y_offset,
				                 x_offset + curve_start_offset,
				                 globals.steps[UP], delta, j);
				delta -= sigma;
			}
		}
		else
		{
			draw_down_bump (buffer, bufsize, width, bytes, y_offset,
			                x_offset + curve_start_offset,
			                globals.steps[DOWN]);
			delta = blend_amount;
			for (j = 0; j < blend_lines; j++)
			{
				/* use to be +j + 1 */
				darken_down_bump (buffer, bufsize, width, bytes, y_offset,
				                  x_offset + curve_start_offset,
				                  globals.steps[DOWN], delta, j);
				/* use to be -j -1 */
				lighten_down_bump (buffer, bufsize, width, bytes, y_offset,
				                   x_offset + curve_start_offset,
				                   globals.steps[DOWN], delta, j);
				delta -= sigma;
			}
		}
		/* ending side wall line */
		px[0] = x_offset + curve_end_offset;
		px[1] = x_offset + curve_end_offset + WALL_XCONS2 * tile_width;
		px[2] = x_offset + curve_end_offset + WALL_XCONS3 * tile_width;
		px[3] = globals.gridx[i];
		py[0] = py[3] = y_offset;
		py[1] = y_offset + WALL_YFACTOR2 * tile_height * FUDGE;
		py[2] = y_offset + WALL_YFACTOR3 * tile_height * FUDGE;
		if (!up)
		{
			py[1] = y_offset - WALL_YFACTOR2 * tile_height;
			py[2] = y_offset - WALL_YFACTOR3 * tile_height;
		}
		generate_bezier (px, py, steps, cachex, cachey);
		draw_bezier_line (buffer, bufsize, width, bytes, steps, cachex, cachey);
		delta = blend_amount;
		for (j = 0; j < blend_lines; j++)
		{
			py[0] =  -j - 1;
			darken_bezier_line (buffer, bufsize, width, bytes, 0, py[0],
			                    steps, cachex, cachey, delta);
			py[0] =  j + 1;
			lighten_bezier_line (buffer, bufsize, width, bytes, 0, py[0],
			                     steps, cachex, cachey, delta);
			delta -= sigma;
		}
		x_offset = globals.gridx[i];
	} /* for */
	g_free(cachex);
	g_free(cachey);
}

static void
check_config (gint width,
              gint height)
{
	gint tile_width, tile_height;
	gint tile_width_limit, tile_height_limit;

	if (config.x < 1)
	{
		config.x = 1;
	}
	if (config.y < 1)
	{
		config.y = 1;
	}
	if (config.blend_amount < 0)
	{
		config.blend_amount = 0;
	}
	if (config.blend_amount > 5)
	{
		config.blend_amount = 5;
	}
	tile_width = width / config.x;
	tile_height = height / config.y;
	tile_width_limit = 0.4 * tile_width;
	tile_height_limit = 0.4 * tile_height;
	if ((config.blend_lines > tile_width_limit)
	    || (config.blend_lines > tile_height_limit))
	{
		config.blend_lines = MIN(tile_width_limit, tile_height_limit);
	}
}

/********************************************************
   GUI
********************************************************/

static gboolean
jigsaw_dialog (GimpDrawable *drawable)
{
	GtkWidget     *dialog;
	GtkWidget     *main_vbox;
	GtkWidget     *preview;
	GtkSizeGroup  *group;
	GtkWidget     *frame;
	GtkWidget     *rbutton1;
	GtkWidget     *rbutton2;
	GtkWidget     *grid;
	GtkWidget     *scale;
	gboolean run;

	gimp_ui_init (PLUG_IN_BINARY);

	dialog = gimp_dialog_new (_("Jigsaw"), PLUG_IN_ROLE,
	                          NULL, 0,
	                          gimp_standard_help_func, PLUG_IN_PROC,

	                          _("_Cancel"), GTK_RESPONSE_CANCEL,
	                          _("_OK"),     GTK_RESPONSE_OK,

	                          NULL);

	gimp_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
	                                          GTK_RESPONSE_OK,
	                                          GTK_RESPONSE_CANCEL,
	                                          -1);

	gimp_window_set_transient (GTK_WINDOW (dialog));

	main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
	                    main_vbox, TRUE, TRUE, 0);
	gtk_widget_show (main_vbox);

	preview = gimp_aspect_preview_new_from_drawable (drawable);
	gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
	gtk_widget_show (preview);

	g_signal_connect_swapped (preview, "invalidated",
	                          G_CALLBACK (jigsaw_preview),
	                          drawable);

	frame = gimp_frame_new (_("Number of Tiles"));
	gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	gtk_container_add (GTK_CONTAINER (frame), grid);

	group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	/* xtiles */
	scale = gimp_scale_entry_new (_("_Horizontal:"), config.x, MIN_XTILES, MAX_XTILES, 0);
	gimp_help_set_help_data (scale, _("Number of pieces going across"), NULL);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 0, 3, 1);
	gtk_widget_show (scale);

	gtk_size_group_add_widget (group, gimp_labeled_get_label (GIMP_LABELED (scale)));
	g_object_unref (group);

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (jigsaw_scale_entry_update_int),
	                  &config.x);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);

	/* ytiles */
	scale = gimp_scale_entry_new (_("_Vertical:"), config.y, MIN_YTILES, MAX_YTILES, 0);
	gimp_help_set_help_data (scale, _("Number of pieces going down"), NULL);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 1, 3, 1);
	gtk_widget_show (scale);

	gtk_size_group_add_widget (group, gimp_labeled_get_label (GIMP_LABELED (scale)));

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (jigsaw_scale_entry_update_int),
	                  &config.y);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);

	gtk_widget_show (grid);
	gtk_widget_show (frame);

	frame = gimp_frame_new (_("Bevel Edges"));
	gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	gtk_container_add (GTK_CONTAINER (frame), grid);

	/* number of blending lines */
	scale = gimp_scale_entry_new (_("_Bevel width:"), config.blend_lines, MIN_BLEND_LINES, MAX_BLEND_LINES, 0);
	gimp_help_set_help_data (scale, _("Degree of slope of each piece's edge"), NULL);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 0, 3, 1);
	gtk_widget_show (scale);

	gtk_size_group_add_widget (group, gimp_labeled_get_label (GIMP_LABELED (scale)));

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (jigsaw_scale_entry_update_int),
	                  &config.blend_lines);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);

	/* blending amount */
	scale = gimp_scale_entry_new (_("H_ighlight:"), config.blend_amount, MIN_BLEND_AMOUNT, MAX_BLEND_AMOUNT, 2);
	gimp_help_set_help_data (scale, _("The amount of highlighting on the edges of each piece"), NULL);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 1, 3, 1);
	gtk_widget_show (scale);

	gtk_size_group_add_widget (group, gimp_labeled_get_label (GIMP_LABELED (scale)));

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (jigsaw_scale_entry_update_double),
	                  &config.blend_amount);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);

	gtk_widget_show (grid);
	gtk_widget_show (frame);

	/* frame for primitive radio buttons */

	frame = gimp_int_radio_group_new (TRUE, _("Jigsaw Style"),
	                                  G_CALLBACK (gimp_radio_button_update),
	                                  &config.style, NULL, config.style,

	                                  _("_Square"), BEZIER_1, &rbutton1,
	                                  _("C_urved"), BEZIER_2, &rbutton2,

	                                  NULL);

	gimp_help_set_help_data (rbutton1, _("Each piece has straight sides"), NULL);
	gimp_help_set_help_data (rbutton2, _("Each piece has curved sides"),   NULL);
	g_signal_connect_swapped (rbutton1, "toggled",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);
	g_signal_connect_swapped (rbutton2, "toggled",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);

	gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
	gtk_widget_show (frame);

	gtk_widget_show (dialog);

	run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

	gtk_widget_destroy (dialog);

	return run;
}

static void
jigsaw_scale_entry_update_double (GimpLabelSpin *entry,
                                  gdouble       *value)
{
	*value = gimp_label_spin_get_value (entry);
}

static void
jigsaw_scale_entry_update_int (GimpLabelSpin *entry,
                               gint          *value)
{
	*value = (gint) gimp_label_spin_get_value (entry);
}
