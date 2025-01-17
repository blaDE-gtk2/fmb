/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2007 Nick Schermer <nick@xfce.org>
 * Copyright (c) 2007 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2009 Jannis Pohlmann <jannis@xfce.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#include <blxo/blxo.h>

#include <fmb-sbr/fmb-sbr-date-renamer.h>

#ifdef HAVE_EXIF
#include <libexif/exif-data.h>
#endif



/* Property identifiers */
enum
{
  PROP_0,
  PROP_MODE,
  PROP_FORMAT,
  PROP_OFFSET,
  PROP_OFFSET_MODE,
};



static void    fmb_sbr_date_renamer_finalize     (GObject                   *object);
static void    fmb_sbr_date_renamer_get_property (GObject                   *object,
                                                     guint                      prop_id,
                                                     GValue                    *value,
                                                     GParamSpec                *pspec);
static void    fmb_sbr_date_renamer_set_property (GObject                   *object,
                                                     guint                      prop_id,
                                                     const GValue              *value,
                                                     GParamSpec                *pspec);
static gchar  *fmb_sbr_get_time_string           (guint64                    file_time,
                                                     const gchar               *custom_format);
#ifdef HAVE_EXIF
static guint64 fmb_sbr_get_time_from_string      (const gchar               *string);
#endif
static guint64 fmb_sbr_get_time                  (FmbxFileInfo           *file,
                                                     FmbSbrDateMode          mode);
static gchar  *fmb_sbr_date_renamer_process      (FmbxRenamer            *renamer,
                                                     FmbxFileInfo           *file,
                                                     const gchar               *text,
                                                     guint                      idx);



struct _FmbSbrDateRenamerClass
{
  FmbxRenamerClass __parent__;
};

struct _FmbSbrDateRenamer
{
  FmbxRenamer      __parent__;
  FmbSbrDateMode   mode;
  guint               offset;
  FmbSbrOffsetMode offset_mode;
  gchar              *format;
};



FMBX_DEFINE_TYPE (FmbSbrDateRenamer, fmb_sbr_date_renamer, FMBX_TYPE_RENAMER);



static void
fmb_sbr_date_renamer_class_init (FmbSbrDateRenamerClass *klass)
{
  FmbxRenamerClass *fmbxrenamer_class;
  GObjectClass        *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_sbr_date_renamer_finalize;
  gobject_class->get_property = fmb_sbr_date_renamer_get_property;
  gobject_class->set_property = fmb_sbr_date_renamer_set_property;

  fmbxrenamer_class = FMBX_RENAMER_CLASS (klass);
  fmbxrenamer_class->process = fmb_sbr_date_renamer_process;

  /**
   * FmbSbrDateRenamer:mode:
   *
   * The #FmbSbrDateMode to use.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_MODE,
                                   g_param_spec_enum ("mode",
                                                      "mode",
                                                      "mode",
                                                      FMB_SBR_TYPE_DATE_MODE,
                                                      FMB_SBR_DATE_MODE_NOW,
                                                      G_PARAM_READWRITE));

  /**
   * FmbSbrDateRenamer:format:
   *
   * The date format to insert.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_FORMAT,
                                   g_param_spec_string ("format",
                                                        "format",
                                                        "format",
                                                        "%Y%m%d",
                                                        G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  /**
   * FmbSbrDateRenamer:offset:
   *
   * The starting offset at which to insert/overwrite.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_OFFSET,
                                   g_param_spec_uint ("offset",
                                                      "offset",
                                                      "offset",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_READWRITE));

  /**
   * FmbSbrDateRenamer:offset-mode:
   *
   * The offset mode for the renamer.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_OFFSET_MODE,
                                   g_param_spec_enum ("offset-mode",
                                                      "offset-mode",
                                                      "offset-mode",
                                                      FMB_SBR_TYPE_OFFSET_MODE,
                                                      FMB_SBR_OFFSET_MODE_LEFT,
                                                      G_PARAM_READWRITE));
}



static void
fmb_sbr_date_renamer_init (FmbSbrDateRenamer *date_renamer)
{
  GEnumClass     *klass;
  AtkRelationSet *relations;
  AtkRelation    *relation;
  AtkObject      *object;
  GtkWidget      *vbox, *hbox;
  GtkWidget      *label, *combo;
  GtkWidget      *spinner;
  GtkWidget      *entry;
  GtkAdjustment  *adjustment;
  guint           n;

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (date_renamer), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("Insert _time:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  combo = gtk_combo_box_new_text ();
  klass = g_type_class_ref (FMB_SBR_TYPE_DATE_MODE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _(klass->values[n].value_nick));
  blxo_mutual_binding_new (G_OBJECT (date_renamer), "mode", G_OBJECT (combo), "active");
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  g_type_class_unref (klass);
  gtk_widget_show (combo);

  /* set Atk label relation for the combo */
  object = gtk_widget_get_accessible (combo);
  relations = atk_object_ref_relation_set (gtk_widget_get_accessible (label));
  relation = atk_relation_new (&object, 1, ATK_RELATION_LABEL_FOR);
  atk_relation_set_add (relations, relation);
  g_object_unref (G_OBJECT (relation));

  label = gtk_label_new_with_mnemonic (_("_Format:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  blxo_mutual_binding_new (G_OBJECT (entry), "text", G_OBJECT (date_renamer), "format");
  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text (entry,
                               _("The format describes the date and time parts to insert "
                                "into the file name. For example, %Y will be substituted "
                                "with the year, %m with the month and %d with the day. "
                                "See the documentation of the date utility for additional "
                                "information."));
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  gtk_widget_show (entry);

  /* set Atk label relation for the entry */
  object = gtk_widget_get_accessible (entry);
  relations = atk_object_ref_relation_set (gtk_widget_get_accessible (label));
  relation = atk_relation_new (&object, 1, ATK_RELATION_LABEL_FOR);
  atk_relation_set_add (relations, relation);
  g_object_unref (G_OBJECT (relation));

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("_At position:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  spinner = gtk_spin_button_new_with_range (0u, G_MAXUINT, 1u);
  gtk_entry_set_width_chars (GTK_ENTRY (spinner), 4);
  gtk_entry_set_alignment (GTK_ENTRY (spinner), 1.0f);
  gtk_entry_set_activates_default (GTK_ENTRY (spinner), TRUE);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spinner), 0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinner), TRUE);
  gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (spinner), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox), spinner, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), spinner);
  gtk_widget_show (spinner);

  adjustment = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (spinner));
  blxo_mutual_binding_new (G_OBJECT (date_renamer), "offset", G_OBJECT (adjustment), "value");

  combo = gtk_combo_box_new_text ();
  klass = g_type_class_ref (FMB_SBR_TYPE_OFFSET_MODE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _(klass->values[n].value_nick));
  blxo_mutual_binding_new (G_OBJECT (date_renamer), "offset-mode", G_OBJECT (combo), "active");
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
  g_type_class_unref (klass);
  gtk_widget_show (combo);
}



static void
fmb_sbr_date_renamer_finalize (GObject *object)
{
  FmbSbrDateRenamer *date_renamer = FMB_SBR_DATE_RENAMER (object);

  /* release the format */
  g_free (date_renamer->format);

  (*G_OBJECT_CLASS (fmb_sbr_date_renamer_parent_class)->finalize) (object);
}



static void
fmb_sbr_date_renamer_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  FmbSbrDateRenamer *date_renamer = FMB_SBR_DATE_RENAMER (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value, fmb_sbr_date_renamer_get_mode (date_renamer));
      break;

    case PROP_FORMAT:
      g_value_set_string (value, fmb_sbr_date_renamer_get_format (date_renamer));
      break;

    case PROP_OFFSET:
      g_value_set_uint (value, fmb_sbr_date_renamer_get_offset (date_renamer));
      break;

    case PROP_OFFSET_MODE:
      g_value_set_enum (value, fmb_sbr_date_renamer_get_offset_mode (date_renamer));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_sbr_date_renamer_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  FmbSbrDateRenamer *date_renamer = FMB_SBR_DATE_RENAMER (object);

  switch (prop_id)
    {
    case PROP_MODE:
      fmb_sbr_date_renamer_set_mode (date_renamer, g_value_get_enum (value));
      break;

    case PROP_FORMAT:
      fmb_sbr_date_renamer_set_format (date_renamer, g_value_get_string (value));
      break;

    case PROP_OFFSET:
      fmb_sbr_date_renamer_set_offset (date_renamer, g_value_get_uint (value));
      break;

    case PROP_OFFSET_MODE:
      fmb_sbr_date_renamer_set_offset_mode (date_renamer, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gchar *
fmb_sbr_get_time_string (guint64      file_time,
                            const gchar *format)
{
  struct tm *tm;
  time_t     _time;
  gchar     *converted;
  gchar      buffer[1024];
  gint       length;

  _time = (time_t) file_time;

  /* determine the local file time */
  tm = localtime (&_time);

  /* conver the format to the current locale */
  converted = g_locale_from_utf8 (format, -1, NULL, NULL, NULL);

  /* parse the format */
  length = strftime (buffer, sizeof (buffer), converted, tm);

  /* cleanup */
  g_free (converted);

  /* check if strftime succeeded */
  if (G_UNLIKELY (length == 0))
    return NULL;

  /* return the utf-8 string */
  return g_locale_to_utf8 (buffer, -1, NULL, NULL, NULL);
}



#ifdef HAVE_EXIF
static guint64
fmb_sbr_get_time_from_string (const gchar *string)
{
  struct tm tm;

#ifdef HAVE_STRPTIME
  /* parse the string with strptime */
  strptime (string, "%Y:%m:%d %T", &tm);
#else
  gint result;
  gint year, month, day, hour, min, sec;

  result = sscanf (string, "%d:%d:%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec);

  /* only continue when atleast the date is parsed succesfully */
  if (G_LIKELY (result >= 3 && g_date_valid_dmy (day, month, year)))
    {
      /* be sure to start with a clean tm */
      memset (&tm, 0, sizeof (tm));

      /* set the date */
      tm.tm_year = year - 1900;
      tm.tm_mon = month - 1;
      tm.tm_mday = day;

      /* set the time */
      tm.tm_hour = result >= 4? hour : 0;
      tm.tm_min = result >= 5 ? min : 0;
      tm.tm_sec = result >= 6 ? sec : 0;
    }
  else
    {
      return 0;
    }
#endif
  /* return the local time */
  return mktime (&tm);
}
#endif



static guint64
fmb_sbr_get_time (FmbxFileInfo   *file,
                     FmbSbrDateMode  mode)
{

  GFileInfo *file_info;
  guint64    file_time = 0;
#ifdef HAVE_EXIF
  gchar     *uri, *filename;
  ExifEntry *exif_entry;
  ExifData  *exif_data;
  gchar     exif_buffer[128];
#endif

  switch (mode)
    {
    case FMB_SBR_DATE_MODE_NOW:
      /* set the time to the current time */
      file_time = time (NULL);
      break;

    case FMB_SBR_DATE_MODE_ATIME:
    case FMB_SBR_DATE_MODE_MTIME:
      /* get the file info */
      file_info = fmbx_file_info_get_file_info (file);

      /* get the time from the info */
      if (mode == FMB_SBR_DATE_MODE_ATIME)
        {
          file_time = g_file_info_get_attribute_uint64 (file_info, 
                                                        G_FILE_ATTRIBUTE_TIME_ACCESS);
        }
      else
        {
          file_time = g_file_info_get_attribute_uint64 (file_info,
                                                        G_FILE_ATTRIBUTE_TIME_MODIFIED);
        }

      /* release the file info */
      g_object_unref (file_info);
      break;

#ifdef HAVE_EXIF
    case FMB_SBR_DATE_MODE_TAKEN:
      /* get the uri */
      uri = fmbx_file_info_get_uri (file);
      if (G_LIKELY (uri != NULL))
        {
          /* determine the local path of the file */
          filename = g_filename_from_uri (uri, NULL, NULL);
          if (G_LIKELY (filename != NULL))
            {
              /* try to load the exif data for the file */
              exif_data = exif_data_new_from_file (filename);
              if (G_LIKELY (exif_data != NULL))
                {
                  /* lookup the entry for the tag, fallback on less common ones */
                  exif_entry = exif_data_get_entry (exif_data, EXIF_TAG_DATE_TIME);

                  if (exif_entry == NULL)
                    exif_entry = exif_data_get_entry (exif_data, EXIF_TAG_DATE_TIME_ORIGINAL);

                  if (exif_entry == NULL)
                    exif_entry = exif_data_get_entry (exif_data, EXIF_TAG_DATE_TIME_DIGITIZED);

                  if (G_LIKELY (exif_entry != NULL))
                    {
                      /* determine the value */
                      if (exif_entry_get_value (exif_entry, exif_buffer, sizeof (exif_buffer)) != NULL)
                        file_time = fmb_sbr_get_time_from_string (exif_buffer);
                    }

                  /* cleanup */
                  exif_data_free (exif_data);
                }

              /* cleanup */
              g_free (filename);
            }

          /* cleanup */
          g_free (uri);
        }

      break;
#endif
    }

  return file_time;
}



static gchar*
fmb_sbr_date_renamer_process (FmbxRenamer  *renamer,
                                 FmbxFileInfo *file,
                                 const gchar     *text,
                                 guint            idx)
{
  FmbSbrDateRenamer *date_renamer = FMB_SBR_DATE_RENAMER (renamer);
  gchar                *string;
  guint64               file_time;
  const gchar          *s;
  GString              *result;
  guint                 text_length;
  guint                 offset;

  /* return when there is no text in the custom format entry */
  if (G_UNLIKELY (date_renamer->format == NULL || *date_renamer->format == '\0'))
    return g_strdup (text);

  /* determine the input text length */
  text_length = g_utf8_strlen (text, -1);

  /* determine the real offset and check if it's valid */
  offset = (date_renamer->offset_mode == FMB_SBR_OFFSET_MODE_LEFT) ? date_renamer->offset : (text_length - date_renamer->offset);
  if (G_UNLIKELY (offset > text_length))
    return g_strdup (text);

  /* get the file time */
  file_time = fmb_sbr_get_time (file, date_renamer->mode);
  if (file_time == 0)
    return g_strdup (text);

  /* allocate space for the result */
  result = g_string_sized_new (2 * text_length);

  /* determine the text pointer for the offset */
  s = g_utf8_offset_to_pointer (text, offset);

  /* add the text before the insert/overwrite offset */
  g_string_append_len (result, text, s - text);

  /* parse the time string */
  string = fmb_sbr_get_time_string (file_time, date_renamer->format);
  if (string)
    {
      g_string_append (result, string);
      g_free (string);
    }

  /* append the remaining text */
  g_string_append (result, s);

  /* return the result */
  return g_string_free (result, FALSE);
}



/**
 * fmb_sbr_date_renamer_new:
 *
 * Allocates a new #FmbSbrDateRenamer instance.
 *
 * Return value: the newly allocated #FmbSbrDateRenamer.
 **/
FmbSbrDateRenamer*
fmb_sbr_date_renamer_new (void)
{
  return g_object_new (FMB_SBR_TYPE_DATE_RENAMER,
                       "name", _("Insert Date / Time"),
                       NULL);
}



/**
 * fmb_sbr_date_renamer_get_mode:
 * @date_renamer : a #FmbSbrDateRenamer.
 *
 * Returns the mode of the @date_renamer.
 *
 * Return value: the mode of @date_renamer.
 **/
FmbSbrDateMode
fmb_sbr_date_renamer_get_mode (FmbSbrDateRenamer *date_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer), FMB_SBR_DATE_MODE_NOW);

  return date_renamer->mode;
}



/**
 * fmb_sbr_date_renamer_set_mode:
 * @date_renamer : a #FmbSbrDateRenamer.
 * @mode         : the new #FmbSbrDateMode for @date_renamer.
 *
 * Sets the mode of @date_renamer to @mode.
 **/
void
fmb_sbr_date_renamer_set_mode (FmbSbrDateRenamer *date_renamer,
                                  FmbSbrDateMode     mode)
{
  g_return_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer));

  /* check if we have a new mode */
  if (G_LIKELY (date_renamer->mode != mode))
    {
      /* apply the new mode */
      date_renamer->mode = mode;

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (date_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (date_renamer), "mode");
    }
}



/**
 * fmb_sbr_date_renamer_get_format:
 * @date_renamer : a #FmbSbrDateRenamer.
 *
 * Returns the format for the @date_renamer.
 *
 * Return value: the format for @date_renamer.
 **/
const gchar*
fmb_sbr_date_renamer_get_format (FmbSbrDateRenamer *date_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer), NULL);

  return date_renamer->format;
}



/**
 * fmb_sbr_date_renamer_set_format:
 * @date_renamer : a #FmbSbrDateRenamer.
 * @format       : the new format for @date_renamer.
 *
 * Sets the format for @date_renamer to @format.
 **/
void
fmb_sbr_date_renamer_set_format (FmbSbrDateRenamer *date_renamer,
                                    const gchar          *format)
{
  g_return_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer));

  /* check if we have a new format */
  if (G_LIKELY (!blxo_str_is_equal (date_renamer->format, format)))
    {
      /* apply the new format */
      g_free (date_renamer->format);
      date_renamer->format = g_strdup (format);

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (date_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (date_renamer), "format");
    }
}



/**
 * fmb_sbr_date_renamer_get_offset:
 * @date_renamer : a #FmbSbrInsertRenamer.
 *
 * Returns the offset for the @date_renamer.
 *
 * Return value: the offset for @date_renamer.
 **/
guint
fmb_sbr_date_renamer_get_offset (FmbSbrDateRenamer *date_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer), 0);
  return date_renamer->offset;
}



/**
 * fmb_sbr_date_renamer_set_offset:
 * @date_renamer : a #FmbSbrInsertRenamer.
 * @offset         : the new offset for @date_renamer.
 *
 * Sets the offset for @date_renamer to @offset.
 **/
void
fmb_sbr_date_renamer_set_offset (FmbSbrDateRenamer *date_renamer,
                                    guint                 offset)
{
  g_return_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer));

  /* check if we have a new offset */
  if (G_LIKELY (date_renamer->offset != offset))
    {
      /* apply the new offset */
      date_renamer->offset = offset;

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (date_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (date_renamer), "offset");
    }
}



/**
 * fmb_sbr_date_renamer_get_offset_mode:
 * @date_renamer : a #FmbSbrInsertRenamer.
 *
 * Returns the offset mode for the @date_renamer.
 *
 * Return value: the offset mode for @date_renamer.
 **/
FmbSbrOffsetMode
fmb_sbr_date_renamer_get_offset_mode (FmbSbrDateRenamer *date_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer), FMB_SBR_OFFSET_MODE_LEFT);
  return date_renamer->offset_mode;
}



/**
 * fmb_sbr_date_renamer_set_offset_mode:
 * @date_renamer : a #FmbSbrInsertRenamer.
 * @offset_mode    : the new offset mode for @date_renamer.
 *
 * Sets the offset mode for @date_renamer to @offset_mode.
 **/
void
fmb_sbr_date_renamer_set_offset_mode (FmbSbrDateRenamer *date_renamer,
                                         FmbSbrOffsetMode   offset_mode)
{
  g_return_if_fail (FMB_SBR_IS_DATE_RENAMER (date_renamer));

  /* check if we have a new setting */
  if (G_LIKELY (date_renamer->offset_mode != offset_mode))
    {
      /* apply the new setting */
      date_renamer->offset_mode = offset_mode;

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (date_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (date_renamer), "offset-mode");
    }
}
