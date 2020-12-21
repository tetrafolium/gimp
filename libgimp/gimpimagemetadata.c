/* LIBGIMP - The GIMP Library
 * Copyright (C) 1995-2000 Peter Mattis and Spencer Kimball
 *
 * gimpimagemetadata.c
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>
#include <sys/time.h>

#include <gexiv2/gexiv2.h>

#include "gimp.h"
#include "gimpimagemetadata.h"

#include "libgimp-intl.h"


typedef struct
{
	gchar *tag;
	gint type;
} XmpStructs;


static void        gimp_image_metadata_rotate        (GimpImage         *image,
                                                      GExiv2Orientation orientation);

/*  public functions  */

/**
 * gimp_image_metadata_load_prepare:
 * @image:     The image
 * @mime_type: The loaded file's mime-type
 * @file:      The file to load the metadata from
 * @error:     Return location for error
 *
 * Loads and returns metadata from @file to be passed into
 * gimp_image_metadata_load_finish().
 *
 * Returns: (transfer full): The file's metadata.
 *
 * Since: 2.10
 */
GimpMetadata *
gimp_image_metadata_load_prepare (GimpImage    *image,
                                  const gchar  *mime_type,
                                  GFile        *file,
                                  GError      **error)
{
	GimpMetadata *metadata;

	g_return_val_if_fail (GIMP_IS_IMAGE (image), NULL);
	g_return_val_if_fail (mime_type != NULL, NULL);
	g_return_val_if_fail (G_IS_FILE (file), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	metadata = gimp_metadata_load_from_file (file, error);

	if (metadata)
	{
		gexiv2_metadata_erase_exif_thumbnail (GEXIV2_METADATA (metadata));
	}

	return metadata;
}

/**
 * gimp_image_metadata_load_finish:
 * @image:       The image
 * @mime_type:   The loaded file's mime-type
 * @metadata:    The metadata to set on the image
 * @flags:       Flags to specify what of the metadata to apply to the image
 *
 * Applies the @metadata previously loaded with
 * gimp_image_metadata_load_prepare() to the image, taking into account
 * the passed @flags.
 *
 * Since: 3.0
 */
void
gimp_image_metadata_load_finish (GimpImage             *image,
                                 const gchar           *mime_type,
                                 GimpMetadata          *metadata,
                                 GimpMetadataLoadFlags flags)
{
	g_return_if_fail (GIMP_IS_IMAGE (image));
	g_return_if_fail (mime_type != NULL);
	g_return_if_fail (GEXIV2_IS_METADATA (metadata));

	if (flags & GIMP_METADATA_LOAD_COMMENT)
	{
		gchar *comment;

		comment = gexiv2_metadata_get_tag_interpreted_string (GEXIV2_METADATA (metadata),
		                                                      "Exif.Photo.UserComment");
		if (!comment)
			comment = gexiv2_metadata_get_tag_interpreted_string (GEXIV2_METADATA (metadata),
			                                                      "Exif.Image.ImageDescription");

		if (comment)
		{
			GimpParasite *parasite;

			parasite = gimp_parasite_new ("gimp-comment",
			                              GIMP_PARASITE_PERSISTENT,
			                              strlen (comment) + 1,
			                              comment);
			g_free (comment);

			gimp_image_attach_parasite (image, parasite);
			gimp_parasite_free (parasite);
		}
	}

	if (flags & GIMP_METADATA_LOAD_RESOLUTION)
	{
		gdouble xres;
		gdouble yres;
		GimpUnit unit;

		if (gimp_metadata_get_resolution (metadata, &xres, &yres, &unit))
		{
			gimp_image_set_resolution (image, xres, yres);
			gimp_image_set_unit (image, unit);
		}
	}

	if (!(flags & GIMP_METADATA_LOAD_ORIENTATION))
	{
		gexiv2_metadata_set_orientation (GEXIV2_METADATA (metadata),
		                                 GEXIV2_ORIENTATION_UNSPECIFIED);
	}

	if (flags & GIMP_METADATA_LOAD_COLORSPACE)
	{
		GimpColorProfile *profile = gimp_image_get_color_profile (image);

		/* only look for colorspace information from metadata if the
		 * image didn't contain an embedded color profile
		 */
		if (!profile)
		{
			GimpMetadataColorspace colorspace;

			colorspace = gimp_metadata_get_colorspace (metadata);

			switch (colorspace)
			{
			case GIMP_METADATA_COLORSPACE_UNSPECIFIED:
			case GIMP_METADATA_COLORSPACE_UNCALIBRATED:
			case GIMP_METADATA_COLORSPACE_SRGB:
				/* use sRGB, a NULL profile will do the right thing  */
				break;

			case GIMP_METADATA_COLORSPACE_ADOBERGB:
				profile = gimp_color_profile_new_rgb_adobe ();
				break;
			}

			if (profile)
				gimp_image_set_color_profile (image, profile);
		}

		if (profile)
			g_object_unref (profile);
	}

	gimp_image_set_metadata (image, metadata);
}

/**
 * gimp_image_metadata_load_thumbnail:
 * @file:  A #GFile image
 * @error: Return location for error message
 *
 * Retrieves a thumbnail from metadata if present.
 *
 * Returns: (transfer none) (nullable): a #GimpImage of the @file thumbnail.
 *
 * Since: 2.10
 */
GimpImage *
gimp_image_metadata_load_thumbnail (GFile   *file,
                                    GError **error)
{
	GimpMetadata *metadata;
	GInputStream *input_stream;
	GdkPixbuf    *pixbuf;
	guint8       *thumbnail_buffer;
	gint thumbnail_size;
	GimpImage    *image = NULL;

	g_return_val_if_fail (G_IS_FILE (file), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	metadata = gimp_metadata_load_from_file (file, error);
	if (!metadata)
		return NULL;

	if (!gexiv2_metadata_get_exif_thumbnail (GEXIV2_METADATA (metadata),
	                                         &thumbnail_buffer,
	                                         &thumbnail_size))
	{
		g_object_unref (metadata);
		return NULL;
	}

	input_stream = g_memory_input_stream_new_from_data (thumbnail_buffer,
	                                                    thumbnail_size,
	                                                    (GDestroyNotify) g_free);
	pixbuf = gdk_pixbuf_new_from_stream (input_stream, NULL, error);
	g_object_unref (input_stream);

	if (pixbuf)
	{
		GimpLayer *layer;

		image = gimp_image_new (gdk_pixbuf_get_width  (pixbuf),
		                        gdk_pixbuf_get_height (pixbuf),
		                        GIMP_RGB);
		gimp_image_undo_disable (image);

		layer = gimp_layer_new_from_pixbuf (image, _("Background"),
		                                    pixbuf,
		                                    100.0,
		                                    gimp_image_get_default_new_layer_mode (image),
		                                    0.0, 0.0);
		g_object_unref (pixbuf);

		gimp_image_insert_layer (image, layer, NULL, 0);

		gimp_image_metadata_rotate (image,
		                            gexiv2_metadata_get_orientation (GEXIV2_METADATA (metadata)));
	}

	g_object_unref (metadata);

	return image;
}


/*  private functions  */

static void
gimp_image_metadata_rotate (GimpImage         *image,
                            GExiv2Orientation orientation)
{
	switch (orientation)
	{
	case GEXIV2_ORIENTATION_UNSPECIFIED:
	case GEXIV2_ORIENTATION_NORMAL: /* standard orientation, do nothing */
		break;

	case GEXIV2_ORIENTATION_HFLIP:
		gimp_image_flip (image, GIMP_ORIENTATION_HORIZONTAL);
		break;

	case GEXIV2_ORIENTATION_ROT_180:
		gimp_image_rotate (image, GIMP_ROTATE_180);
		break;

	case GEXIV2_ORIENTATION_VFLIP:
		gimp_image_flip (image, GIMP_ORIENTATION_VERTICAL);
		break;

	case GEXIV2_ORIENTATION_ROT_90_HFLIP: /* flipped diagonally around '\' */
		gimp_image_rotate (image, GIMP_ROTATE_90);
		gimp_image_flip (image, GIMP_ORIENTATION_HORIZONTAL);
		break;

	case GEXIV2_ORIENTATION_ROT_90: /* 90 CW */
		gimp_image_rotate (image, GIMP_ROTATE_90);
		break;

	case GEXIV2_ORIENTATION_ROT_90_VFLIP: /* flipped diagonally around '/' */
		gimp_image_rotate (image, GIMP_ROTATE_90);
		gimp_image_flip (image, GIMP_ORIENTATION_VERTICAL);
		break;

	case GEXIV2_ORIENTATION_ROT_270: /* 90 CCW */
		gimp_image_rotate (image, GIMP_ROTATE_270);
		break;

	default: /* shouldn't happen */
		break;
	}
}
