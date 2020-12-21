/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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
 */

#include "config.h"

#include <string.h>

#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#include "libgimp/stdplugins-intl.h"


#define PLUG_IN_PROC        "plug-in-retinex"
#define PLUG_IN_BINARY      "contrast-retinex"
#define PLUG_IN_ROLE        "gimp-contrast-retinex"
#define MAX_RETINEX_SCALES    8
#define MIN_GAUSSIAN_SCALE   16
#define MAX_GAUSSIAN_SCALE  250


typedef struct
{
	gint scale;
	gint nscales;
	gint scales_mode;
	gfloat cvar;
} RetinexParams;

typedef enum
{
	filter_uniform,
	filter_low,
	filter_high
} FilterMode;

/*
   Definit comment sont repartis les
   differents filtres en fonction de
   l'echelle (~= ecart type de la gaussienne)
 */
#define RETINEX_UNIFORM 0
#define RETINEX_LOW     1
#define RETINEX_HIGH    2

static gfloat RetinexScales[MAX_RETINEX_SCALES];

typedef struct
{
	gint N;
	gfloat sigma;
	gdouble B;
	gdouble b[4];
} gauss3_coefs;


typedef struct _Retinex Retinex;
typedef struct _RetinexClass RetinexClass;

struct _Retinex
{
	GimpPlugIn parent_instance;
};

struct _RetinexClass
{
	GimpPlugInClass parent_class;
};


#define RETINEX_TYPE  (retinex_get_type ())
#define RETINEX (obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RETINEX_TYPE, Retinex))

GType                   retinex_get_type         (void) G_GNUC_CONST;

static GList          * retinex_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure  * retinex_create_procedure (GimpPlugIn           *plug_in,
                                                  const gchar          *name);

static GimpValueArray * retinex_run              (GimpProcedure        *procedure,
                                                  GimpRunMode run_mode,
                                                  GimpImage            *image,
                                                  GimpDrawable         *drawable,
                                                  const GimpValueArray *args,
                                                  gpointer run_data);

static gboolean retinex_dialog              (GimpDrawable *drawable);
static void     retinex                     (GimpDrawable *drawable,
                                             GimpPreview  *preview);
static void     retinex_preview             (GimpDrawable *drawable,
                                             GimpPreview  *preview);

static void     retinex_scales_distribution (gfloat       *scales,
                                             gint nscales,
                                             gint mode,
                                             gint s);

static void     compute_mean_var            (gfloat       *src,
                                             gfloat       *mean,
                                             gfloat       *var,
                                             gint size,
                                             gint bytes);

static void     compute_coefs3              (gauss3_coefs *c,
                                             gfloat sigma);

static void     gausssmooth                 (gfloat       *in,
                                             gfloat       *out,
                                             gint size,
                                             gint rowtride,
                                             gauss3_coefs *c);

static void contrast_retinex_scale_entry_update_int   (GimpLabelSpin *entry,
                                                       gint          *value);
static void contrast_retinex_scale_entry_update_float (GimpLabelSpin *entry,
                                                       gfloat        *value);

/*
 * MSRCR = MultiScale Retinex with Color Restoration
 */
static void     MSRCR                       (guchar       *src,
                                             gint width,
                                             gint height,
                                             gint bytes,
                                             gboolean preview_mode);


G_DEFINE_TYPE (Retinex, retinex, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (RETINEX_TYPE)


static RetinexParams rvals =
{
	240,         /* Scale */
	3,           /* Scales */
	RETINEX_UNIFORM, /* Echelles reparties uniformement */
	1.2          /* A voir */
};


static void
retinex_class_init (RetinexClass *klass)
{
	GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

	plug_in_class->query_procedures = retinex_query_procedures;
	plug_in_class->create_procedure = retinex_create_procedure;
}

static void
retinex_init (Retinex *retinex)
{
}

static GList *
retinex_query_procedures (GimpPlugIn *plug_in)
{
	return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static GimpProcedure *
retinex_create_procedure (GimpPlugIn  *plug_in,
                          const gchar *name)
{
	GimpProcedure *procedure = NULL;

	if (!strcmp (name, PLUG_IN_PROC))
	{
		procedure = gimp_image_procedure_new (plug_in, name,
		                                      GIMP_PDB_PROC_TYPE_PLUGIN,
		                                      retinex_run, NULL, NULL);

		gimp_procedure_set_image_types (procedure, "RGB*");

		gimp_procedure_set_menu_label (procedure, N_("Retine_x..."));
		gimp_procedure_add_menu_path (procedure, "<Image>/Colors/Tone Mapping");

		gimp_procedure_set_documentation (procedure,
		                                  N_("Enhance contrast using the "
		                                     "Retinex method"),
		                                  "The Retinex Image Enhancement "
		                                  "Algorithm is an automatic image "
		                                  "enhancement method that enhances "
		                                  "a digital image in terms of dynamic "
		                                  "range compression, color independence "
		                                  "from the spectral distribution of the "
		                                  "scene illuminant, and color/lightness "
		                                  "rendition.",
		                                  name);
		gimp_procedure_set_attribution (procedure,
		                                "Fabien Pelisson",
		                                "Fabien Pelisson",
		                                "2003");

		GIMP_PROC_ARG_INT (procedure, "scale",
		                   "Scale",
		                   "Biggest scale value",
		                   MIN_GAUSSIAN_SCALE, MAX_GAUSSIAN_SCALE, 240,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_INT (procedure, "nscales",
		                   "N scales",
		                   "Number of scales",
		                   0, MAX_RETINEX_SCALES, 3,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_INT (procedure, "scales-mode",
		                   "Scales mode",
		                   "Retinex distribution through scales",
		                   RETINEX_UNIFORM, RETINEX_HIGH, RETINEX_UNIFORM,
		                   G_PARAM_READWRITE);

		GIMP_PROC_ARG_DOUBLE (procedure, "cvar",
		                      "Cvar",
		                      "Variance value",
		                      0, 4, 1.2,
		                      G_PARAM_READWRITE);
	}

	return procedure;
}

static GimpValueArray *
retinex_run (GimpProcedure        *procedure,
             GimpRunMode run_mode,
             GimpImage            *image,
             GimpDrawable         *drawable,
             const GimpValueArray *args,
             gpointer run_data)
{
	gint x, y, width, height;

	INIT_I18N ();
	gegl_init (NULL, NULL);

	if (!gimp_drawable_mask_intersect (drawable, &x, &y, &width, &height) ||
	    width  < MIN_GAUSSIAN_SCALE ||
	    height < MIN_GAUSSIAN_SCALE)
	{
		return gimp_procedure_new_return_values (procedure,
		                                         GIMP_PDB_EXECUTION_ERROR,
		                                         NULL);
	}

	switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
		gimp_get_data (PLUG_IN_PROC, &rvals);

		if (!retinex_dialog (drawable))
		{
			return gimp_procedure_new_return_values (procedure,
			                                         GIMP_PDB_CANCEL,
			                                         NULL);
		}
		break;

	case GIMP_RUN_NONINTERACTIVE:
		rvals.scale        = GIMP_VALUES_GET_INT    (args, 0);
		rvals.nscales      = GIMP_VALUES_GET_INT    (args, 1);
		rvals.scales_mode  = GIMP_VALUES_GET_INT    (args, 2);
		rvals.cvar         = GIMP_VALUES_GET_DOUBLE (args, 3);
		break;

	case GIMP_RUN_WITH_LAST_VALS:
		gimp_get_data (PLUG_IN_PROC, &rvals);
		break;

	default:
		break;
	}

	if (gimp_drawable_is_rgb (drawable))
	{
		gimp_progress_init (_("Retinex"));

		retinex (drawable, NULL);

		if (run_mode != GIMP_RUN_NONINTERACTIVE)
			gimp_displays_flush ();

		if (run_mode == GIMP_RUN_INTERACTIVE)
			gimp_set_data (PLUG_IN_PROC, &rvals, sizeof (RetinexParams));
	}
	else
	{
		return gimp_procedure_new_return_values (procedure,
		                                         GIMP_PDB_EXECUTION_ERROR,
		                                         NULL);
	}

	return gimp_procedure_new_return_values (procedure, GIMP_PDB_SUCCESS, NULL);
}


static gboolean
retinex_dialog (GimpDrawable *drawable)
{
	GtkWidget *dialog;
	GtkWidget *main_vbox;
	GtkWidget *preview;
	GtkWidget *grid;
	GtkWidget *combo;
	GtkWidget *scale;
	gboolean run;

	gimp_ui_init (PLUG_IN_BINARY);

	dialog = gimp_dialog_new (_("Retinex Image Enhancement"), PLUG_IN_ROLE,
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

	preview = gimp_zoom_preview_new_from_drawable (drawable);
	gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
	gtk_widget_show (preview);

	g_signal_connect_swapped (preview, "invalidated",
	                          G_CALLBACK (retinex_preview),
	                          drawable);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	gtk_box_pack_start (GTK_BOX (main_vbox), grid, FALSE, FALSE, 0);
	gtk_widget_show (grid);

	combo = gimp_int_combo_box_new (_("Uniform"), filter_uniform,
	                                _("Low"),     filter_low,
	                                _("High"),    filter_high,
	                                NULL);

	gimp_int_combo_box_connect (GIMP_INT_COMBO_BOX (combo), rvals.scales_mode,
	                            G_CALLBACK (gimp_int_combo_box_get_active),
	                            &rvals.scales_mode, NULL);
	g_signal_connect_swapped (combo, "changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);

	gimp_grid_attach_aligned (GTK_GRID (grid), 0, 0,
	                          _("_Level:"), 0.0, 0.5,
	                          combo, 2);
	gtk_widget_show (combo);

	scale = gimp_scale_entry_new (_("_Scale:"), rvals.scale, MIN_GAUSSIAN_SCALE, MAX_GAUSSIAN_SCALE, 0);

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (contrast_retinex_scale_entry_update_int),
	                  &rvals.scale);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 1, 3, 1);
	gtk_widget_show (scale);

	scale = gimp_scale_entry_new (_("Scale _division:"), rvals.nscales, 0, MAX_RETINEX_SCALES, 0);

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (contrast_retinex_scale_entry_update_int),
	                  &rvals.nscales);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 2, 3, 1);
	gtk_widget_show (scale);

	scale = gimp_scale_entry_new (_("Dy_namic:"), rvals.cvar, 0, 4, 1);

	g_signal_connect (scale, "value-changed",
	                  G_CALLBACK (contrast_retinex_scale_entry_update_float),
	                  &rvals.cvar);
	g_signal_connect_swapped (scale, "value-changed",
	                          G_CALLBACK (gimp_preview_invalidate),
	                          preview);
	gtk_grid_attach (GTK_GRID (grid), scale, 0, 3, 3, 1);
	gtk_widget_show (scale);

	gtk_widget_show (dialog);

	run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

	gtk_widget_destroy (dialog);

	return run;
}

/*
 * Applies the algorithm
 */
static void
retinex (GimpDrawable *drawable,
         GimpPreview *preview)
{
	GeglBuffer *src_buffer;
	GeglBuffer *dest_buffer;
	const Babl *format;
	guchar     *src  = NULL;
	guchar     *psrc = NULL;
	gint x, y, width, height;
	gint size, bytes;

	/*
	 * Get the size of the current image or its selection.
	 */
	if (preview)
	{
		src = gimp_zoom_preview_get_source (GIMP_ZOOM_PREVIEW (preview),
		                                    &width, &height, &bytes);
	}
	else
	{
		if (!gimp_drawable_mask_intersect (drawable,
		                                   &x, &y, &width, &height))
			return;

		if (gimp_drawable_has_alpha (drawable))
			format = babl_format ("R'G'B'A u8");
		else
			format = babl_format ("R'G'B' u8");

		bytes = babl_format_get_bytes_per_pixel (format);

		/* Allocate memory */
		size = width * height * bytes;
		src  = g_try_malloc (sizeof (guchar) * size);

		if (src == NULL)
		{
			g_warning ("Failed to allocate memory");
			return;
		}

		memset (src, 0, sizeof (guchar) * size);

		/* Fill allocated memory with pixel data */
		src_buffer = gimp_drawable_get_buffer (drawable);

		gegl_buffer_get (src_buffer, GEGL_RECTANGLE (x, y, width, height), 1.0,
		                 format, src,
		                 GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
	}

	/*
	   Algorithm for Multi-scale Retinex with color Restoration (MSRCR).
	 */
	psrc = src;
	MSRCR (psrc, width, height, bytes, preview != NULL);

	if (preview)
	{
		gimp_preview_draw_buffer (preview, psrc, width * bytes);
	}
	else
	{
		dest_buffer = gimp_drawable_get_shadow_buffer (drawable);

		gegl_buffer_set (dest_buffer, GEGL_RECTANGLE (x, y, width, height), 0,
		                 format, psrc,
		                 GEGL_AUTO_ROWSTRIDE);

		g_object_unref (src_buffer);
		g_object_unref (dest_buffer);

		gimp_progress_update (1.0);

		gimp_drawable_merge_shadow (drawable, TRUE);
		gimp_drawable_update (drawable, x, y, width, height);
	}

	g_free (src);
}

static void
retinex_preview (GimpDrawable *drawable,
                 GimpPreview  *preview)
{
	retinex (drawable, preview);
}

/*
 * calculate scale values for desired distribution.
 */
static void
retinex_scales_distribution (gfloat *scales,
                             gint nscales,
                             gint mode,
                             gint s)
{
	if (nscales == 1)
	{ /* For one filter we choose the median scale */
		scales[0] = (gint) s / 2;
	}
	else if (nscales == 2)
	{ /* For two filters we choose the median and maximum scale */
		scales[0] = (gint) s / 2;
		scales[1] = (gint) s;
	}
	else
	{
		gfloat size_step = (gfloat) s / (gfloat) nscales;
		gint i;

		switch(mode)
		{
		case RETINEX_UNIFORM:
			for(i = 0; i < nscales; ++i)
				scales[i] = 2. + (gfloat) i * size_step;
			break;

		case RETINEX_LOW:
			size_step = (gfloat) log(s - 2.0) / (gfloat) nscales;
			for (i = 0; i < nscales; ++i)
				scales[i] = 2. + pow (10, (i * size_step) / log (10));
			break;

		case RETINEX_HIGH:
			size_step = (gfloat) log(s - 2.0) / (gfloat) nscales;
			for (i = 0; i < nscales; ++i)
				scales[i] = s - pow (10, (i * size_step) / log (10));
			break;

		default:
			break;
		}
	}
}

/*
 * Calculate the coefficients for the recursive filter algorithm
 * Fast Computation of gaussian blurring.
 */
static void
compute_coefs3 (gauss3_coefs *c, gfloat sigma)
{
	/*
	 * Papers:  "Recursive Implementation of the gaussian filter.",
	 *          Ian T. Young , Lucas J. Van Vliet, Signal Processing 44, Elsevier 1995.
	 * formula: 11b       computation of q
	 *          8c        computation of b0..b1
	 *          10        alpha is normalization constant B
	 */
	gfloat q, q2, q3;

	if (sigma >= 2.5)
	{
		q = 0.98711 * sigma - 0.96330;
	}
	else if ((sigma >= 0.5) && (sigma < 2.5))
	{
		q = 3.97156 - 4.14554 * (gfloat) sqrt ((double) 1 - 0.26891 * sigma);
	}
	else
	{
		q = 0.1147705018520355224609375;
	}

	q2 = q * q;
	q3 = q * q2;
	c->b[0] = (1.57825+(2.44413*q)+(1.4281 *q2)+(0.422205*q3));
	c->b[1] = (        (2.44413*q)+(2.85619*q2)+(1.26661 *q3));
	c->b[2] = (                   -((1.4281*q2)+(1.26661 *q3)));
	c->b[3] = (                                 (0.422205*q3));
	c->B = 1.0-((c->b[1]+c->b[2]+c->b[3])/c->b[0]);
	c->sigma = sigma;
	c->N = 3;

	/*
	   g_printerr ("q %f\n", q);
	   g_printerr ("q2 %f\n", q2);
	   g_printerr ("q3 %f\n", q3);
	   g_printerr ("c->b[0] %f\n", c->b[0]);
	   g_printerr ("c->b[1] %f\n", c->b[1]);
	   g_printerr ("c->b[2] %f\n", c->b[2]);
	   g_printerr ("c->b[3] %f\n", c->b[3]);
	   g_printerr ("c->B %f\n", c->B);
	   g_printerr ("c->sigma %f\n", c->sigma);
	   g_printerr ("c->N %d\n", c->N);
	 */
}

static void
gausssmooth (gfloat *in, gfloat *out, gint size, gint rowstride, gauss3_coefs *c)
{
	/*
	 * Papers:  "Recursive Implementation of the gaussian filter.",
	 *          Ian T. Young , Lucas J. Van Vliet, Signal Processing 44, Elsevier 1995.
	 * formula: 9a        forward filter
	 *          9b        backward filter
	 *          fig7      algorithm
	 */
	gint i,n, bufsize;
	gfloat *w1,*w2;

	/* forward pass */
	bufsize = size+3;
	size -= 1;
	w1 = (gfloat *) g_try_malloc (bufsize * sizeof (gfloat));
	w2 = (gfloat *) g_try_malloc (bufsize * sizeof (gfloat));
	w1[0] = in[0];
	w1[1] = in[0];
	w1[2] = in[0];
	for ( i = 0, n=3; i <= size; i++, n++)
	{
		w1[n] = (gfloat)(c->B*in[i*rowstride] +
		                 ((c->b[1]*w1[n-1] +
		                   c->b[2]*w1[n-2] +
		                   c->b[3]*w1[n-3] ) / c->b[0]));
	}

	/* backward pass */
	w2[size+1]= w1[size+3];
	w2[size+2]= w1[size+3];
	w2[size+3]= w1[size+3];
	for (i = size, n = i; i >= 0; i--, n--)
	{
		w2[n]= out[i * rowstride] = (gfloat)(c->B*w1[n+3] +
		                                     ((c->b[1]*w2[n+1] +
		                                       c->b[2]*w2[n+2] +
		                                       c->b[3]*w2[n+3] ) / c->b[0]));
	}

	g_free (w1);
	g_free (w2);
}

/*
 * This function is the heart of the algo.
 * (a)  Filterings at several scales and sumarize the results.
 * (b)  Calculation of the final values.
 */
static void
MSRCR (guchar *src, gint width, gint height, gint bytes, gboolean preview_mode)
{

	gint scale,row,col;
	gint i,j;
	gint size;
	gint channel;
	guchar       *psrc = NULL;        /* backup pointer for src buffer */
	gfloat       *dst  = NULL;        /* float buffer for algorithm */
	gfloat       *pdst = NULL;        /* backup pointer for float buffer */
	gfloat       *in, *out;
	gint channelsize;                 /* Float memory cache for one channel */
	gfloat weight;
	gauss3_coefs coef;
	gfloat mean, var;
	gfloat mini, range, maxi;
	gfloat alpha;
	gfloat gain;
	gfloat offset;
	gdouble max_preview = 0.0;

	if (!preview_mode)
	{
		gimp_progress_init (_("Retinex: filtering"));
		max_preview = 3 * rvals.nscales;
	}

	/* Allocate all the memory needed for algorithm*/
	size = width * height * bytes;
	dst = g_try_malloc (size * sizeof (gfloat));
	if (dst == NULL)
	{
		g_warning ("Failed to allocate memory");
		return;
	}
	memset (dst, 0, size * sizeof (gfloat));

	channelsize  = (width * height);
	in  = (gfloat *) g_try_malloc (channelsize * sizeof (gfloat));
	if (in == NULL)
	{
		g_free (dst);
		g_warning ("Failed to allocate memory");
		return; /* do some clever stuff */
	}

	out  = (gfloat *) g_try_malloc (channelsize * sizeof (gfloat));
	if (out == NULL)
	{
		g_free (in);
		g_free (dst);
		g_warning ("Failed to allocate memory");
		return; /* do some clever stuff */
	}


	/*
	   Calculate the scales of filtering according to the
	   number of filter and their distribution.
	 */

	retinex_scales_distribution (RetinexScales,
	                             rvals.nscales, rvals.scales_mode, rvals.scale);

	/*
	    Filtering according to the various scales.
	    Summerize the results of the various filters according to a
	    specific weight(here equivalent for all).
	 */
	weight = 1./ (gfloat) rvals.nscales;

	/*
	   The recursive filtering algorithm needs different coefficients according
	   to the selected scale (~ = standard deviation of Gaussian).
	 */
	for (channel = 0; channel < 3; channel++)
	{
		gint pos;

		for (i = 0, pos = channel; i < channelsize; i++, pos += bytes)
		{
			/* 0-255 => 1-256 */
			in[i] = (gfloat)(src[pos] + 1.0);
		}
		for (scale = 0; scale < rvals.nscales; scale++)
		{
			compute_coefs3 (&coef, RetinexScales[scale]);
			/*
			 *  Filtering (smoothing) Gaussian recursive.
			 *
			 *  Filter rows first
			 */
			for (row=0; row < height; row++)
			{
				pos =  row * width;
				gausssmooth (in + pos, out + pos, width, 1, &coef);
			}

			memcpy(in,  out, channelsize * sizeof(gfloat));
			memset(out, 0, channelsize * sizeof(gfloat));

			/*
			 *  Filtering (smoothing) Gaussian recursive.
			 *
			 *  Second columns
			 */
			for (col=0; col < width; col++)
			{
				pos = col;
				gausssmooth(in + pos, out + pos, height, width, &coef);
			}

			/*
			   Summarize the filtered values.
			   In fact one calculates a ratio between the original values and the filtered values.
			 */
			for (i = 0, pos = channel; i < channelsize; i++, pos += bytes)
			{
				dst[pos] += weight * (log (src[pos] + 1.) - log (out[i]));
			}

			if (!preview_mode)
				gimp_progress_update ((channel * rvals.nscales + scale) /
				                      max_preview);
		}
	}
	g_free(in);
	g_free(out);

	/*
	    Final calculation with original value and cumulated filter values.
	    The parameters gain, alpha and offset are constants.
	 */
	/* Ci(x,y)=log[a Ii(x,y)]-log[ Ei=1-s Ii(x,y)] */

	alpha  = 128.;
	gain   = 1.;
	offset = 0.;

	for (i = 0; i < size; i += bytes)
	{
		gfloat logl;

		psrc = src+i;
		pdst = dst+i;

		logl = log((gfloat)psrc[0] + (gfloat)psrc[1] + (gfloat)psrc[2] + 3.);

		pdst[0] = gain * ((log(alpha * (psrc[0]+1.)) - logl) * pdst[0]) + offset;
		pdst[1] = gain * ((log(alpha * (psrc[1]+1.)) - logl) * pdst[1]) + offset;
		pdst[2] = gain * ((log(alpha * (psrc[2]+1.)) - logl) * pdst[2]) + offset;
	}

	/*  if (!preview_mode)
	    gimp_progress_update ((2.0 + (rvals.nscales * 3)) /
	                          ((rvals.nscales * 3) + 3));*/

	/*
	    Adapt the dynamics of the colors according to the statistics of the first and second order.
	    The use of the variance makes it possible to control the degree of saturation of the colors.
	 */
	pdst = dst;

	compute_mean_var (pdst, &mean, &var, size, bytes);
	mini = mean - rvals.cvar*var;
	maxi = mean + rvals.cvar*var;
	range = maxi - mini;

	if (!range)
		range = 1.0;

	for (i = 0; i < size; i+= bytes)
	{
		psrc = src + i;
		pdst = dst + i;

		for (j = 0; j < 3; j++)
		{
			gfloat c = 255 * ( pdst[j] - mini ) / range;

			psrc[j] = (guchar) CLAMP (c, 0, 255);
		}
	}

	g_free (dst);
}

/*
 * Calculate the average and variance in one go.
 */
static void
compute_mean_var (gfloat *src, gfloat *mean, gfloat *var, gint size, gint bytes)
{
	gfloat vsquared;
	gint i,j;
	gfloat *psrc;

	vsquared = 0;
	*mean = 0;
	for (i = 0; i < size; i+= bytes)
	{
		psrc = src+i;
		for (j = 0; j < 3; j++)
		{
			*mean += psrc[j];
			vsquared += psrc[j] * psrc[j];
		}
	}

	*mean /= (gfloat) size; /* mean */
	vsquared /= (gfloat) size; /* mean (x^2) */
	*var = ( vsquared - (*mean * *mean) );
	*var = sqrt(*var); /* var */
}

static void
contrast_retinex_scale_entry_update_int (GimpLabelSpin *entry,
                                         gint          *value)
{
	*value = (gint) gimp_label_spin_get_value (entry);
}

static void
contrast_retinex_scale_entry_update_float (GimpLabelSpin *entry,
                                           gfloat        *value)
{
	*value = (gfloat) gimp_label_spin_get_value (entry);
}
