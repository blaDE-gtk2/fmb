/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2009-2011 Jannis Pohlmann <jannis@xfce.org>
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of 
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write to the Free 
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <fmb/fmb-gobject-extensions.h>
#include <fmb/fmb-icon-factory.h>
#include <fmb/fmb-preferences.h>
#include <fmb/fmb-private.h>
#include <fmb/fmb-thumbnail-frame.h>



/* the timeout until the sweeper is run (in seconds) */
#define FMB_ICON_FACTORY_SWEEP_TIMEOUT (30)



/* Property identifiers */
enum
{
  PROP_0,
  PROP_ICON_THEME,
  PROP_THUMBNAIL_MODE,
};



typedef struct _FmbIconKey FmbIconKey;



static void       fmb_icon_factory_dispose               (GObject                  *object);
static void       fmb_icon_factory_finalize              (GObject                  *object);
static void       fmb_icon_factory_get_property          (GObject                  *object,
                                                             guint                     prop_id,
                                                             GValue                   *value,
                                                             GParamSpec               *pspec);
static void       fmb_icon_factory_set_property          (GObject                  *object,
                                                             guint                     prop_id,
                                                             const GValue             *value,
                                                             GParamSpec               *pspec);
static gboolean   fmb_icon_factory_changed               (GSignalInvocationHint    *ihint,
                                                             guint                     n_param_values,
                                                             const GValue             *param_values,
                                                             gpointer                  user_data);
static gboolean   fmb_icon_factory_sweep_timer           (gpointer                  user_data);
static void       fmb_icon_factory_sweep_timer_destroy   (gpointer                  user_data);
static GdkPixbuf *fmb_icon_factory_load_from_file        (FmbIconFactory        *factory,
                                                             const gchar              *path,
                                                             gint                      size);
static GdkPixbuf *fmb_icon_factory_lookup_icon           (FmbIconFactory        *factory,
                                                             const gchar              *name,
                                                             gint                      size,
                                                             gboolean                  wants_default);
static guint      fmb_icon_key_hash                      (gconstpointer             data);
static gboolean   fmb_icon_key_equal                     (gconstpointer             a,
                                                             gconstpointer             b);
static void       fmb_icon_key_free                      (gpointer                  data);
static GdkPixbuf *fmb_icon_factory_load_fallback         (FmbIconFactory        *factory,
                                                             gint                      size);



struct _FmbIconFactoryClass
{
  GObjectClass __parent__;
};

struct _FmbIconFactory
{
  GObject __parent__;

  FmbPreferences   *preferences;

  GHashTable          *icon_cache;

  GtkIconTheme        *icon_theme;

  FmbThumbnailMode  thumbnail_mode;

  guint                sweep_timer_id;

  gulong               changed_hook_id;

  /* stamp that gets bumped when the theme changes */
  guint                theme_stamp;
};

struct _FmbIconKey
{
  gchar *name;
  gint   size;
};

typedef struct
{
  FmbFileIconState   icon_state;
  FmbFileThumbState  thumb_state;
  gint                  icon_size;
  guint                 stamp;
  GdkPixbuf            *icon;
}
FmbIconStore;



static GQuark fmb_icon_factory_quark = 0;
static GQuark fmb_icon_factory_store_quark = 0;



G_DEFINE_TYPE (FmbIconFactory, fmb_icon_factory, G_TYPE_OBJECT)



static void
fmb_icon_factory_class_init (FmbIconFactoryClass *klass)
{
  GObjectClass *gobject_class;

  fmb_icon_factory_store_quark = g_quark_from_static_string ("fmb-icon-factory-store");

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = fmb_icon_factory_dispose;
  gobject_class->finalize = fmb_icon_factory_finalize;
  gobject_class->get_property = fmb_icon_factory_get_property;
  gobject_class->set_property = fmb_icon_factory_set_property;

  /**
   * FmbIconFactory:icon-theme:
   *
   * The #GtkIconTheme on which the given #FmbIconFactory instance operates
   * on.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_ICON_THEME,
                                   g_param_spec_object ("icon-theme",
                                                        "icon-theme",
                                                        "icon-theme",
                                                        GTK_TYPE_ICON_THEME,
                                                        BLXO_PARAM_READABLE));

  /**
   * FmbIconFactory:thumbnail-mode:
   *
   * Whether this #FmbIconFactory will try to generate and load thumbnails
   * when loading icons for #FmbFile<!---->s.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_THUMBNAIL_MODE,
                                   g_param_spec_enum ("thumbnail-mode",
                                                      "thumbnail-mode",
                                                      "thumbnail-mode",
                                                      FMB_TYPE_THUMBNAIL_MODE,
                                                      FMB_THUMBNAIL_MODE_ONLY_LOCAL,
                                                      BLXO_PARAM_READWRITE));
}



static void
fmb_icon_factory_init (FmbIconFactory *factory)
{
  factory->thumbnail_mode = FMB_THUMBNAIL_MODE_ONLY_LOCAL;

  /* connect emission hook for the "changed" signal on the GtkIconTheme class. We use the emission
   * hook way here, because that way we can make sure that the icon cache is definetly cleared
   * before any other part of the application gets notified about the icon theme change.
   */
  factory->changed_hook_id = g_signal_add_emission_hook (g_signal_lookup ("changed", GTK_TYPE_ICON_THEME),
                                                         0, fmb_icon_factory_changed, factory, NULL);

  /* allocate the hash table for the icon cache */
  factory->icon_cache = g_hash_table_new_full (fmb_icon_key_hash, fmb_icon_key_equal,
                                               fmb_icon_key_free, g_object_unref);
}



static void
fmb_icon_factory_dispose (GObject *object)
{
  FmbIconFactory *factory = FMB_ICON_FACTORY (object);

  _fmb_return_if_fail (FMB_IS_ICON_FACTORY (factory));

  if (G_UNLIKELY (factory->sweep_timer_id != 0))
    g_source_remove (factory->sweep_timer_id);

  (*G_OBJECT_CLASS (fmb_icon_factory_parent_class)->dispose) (object);
}



static void
fmb_icon_factory_finalize (GObject *object)
{
  FmbIconFactory *factory = FMB_ICON_FACTORY (object);

  _fmb_return_if_fail (FMB_IS_ICON_FACTORY (factory));

  /* clear the icon cache hash table */
  g_hash_table_destroy (factory->icon_cache);

  /* remove the "changed" emission hook from the GtkIconTheme class */
  g_signal_remove_emission_hook (g_signal_lookup ("changed", GTK_TYPE_ICON_THEME), factory->changed_hook_id);

  /* disconnect from the associated icon theme (if any) */
  if (G_LIKELY (factory->icon_theme != NULL))
    {
      g_object_set_qdata (G_OBJECT (factory->icon_theme), fmb_icon_factory_quark, NULL);
      g_object_unref (G_OBJECT (factory->icon_theme));
    }

  /* disconnect from the preferences */
  g_object_unref (G_OBJECT (factory->preferences));

  (*G_OBJECT_CLASS (fmb_icon_factory_parent_class)->finalize) (object);
}



static void
fmb_icon_factory_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  FmbIconFactory *factory = FMB_ICON_FACTORY (object);

  switch (prop_id)
    {
    case PROP_ICON_THEME:
      g_value_set_object (value, factory->icon_theme);
      break;

    case PROP_THUMBNAIL_MODE:
      g_value_set_enum (value, factory->thumbnail_mode);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_icon_factory_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  FmbIconFactory *factory = FMB_ICON_FACTORY (object);

  switch (prop_id)
    {
    case PROP_THUMBNAIL_MODE:
      factory->thumbnail_mode = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gboolean
fmb_icon_factory_changed (GSignalInvocationHint *ihint,
                             guint                  n_param_values,
                             const GValue          *param_values,
                             gpointer               user_data)
{
  FmbIconFactory *factory = FMB_ICON_FACTORY (user_data);

  /* drop all items from the icon cache */
  g_hash_table_remove_all (factory->icon_cache);

  /* bump the stamp so all file icons are reloaded */
  factory->theme_stamp++;

  /* keep the emission hook alive */
  return TRUE;
}



static gboolean
fmb_icon_check_sweep (FmbIconKey *key,
                         GdkPixbuf     *pixbuf)
{
  return (G_OBJECT (pixbuf)->ref_count == 1);
}



static gboolean
fmb_icon_factory_sweep_timer (gpointer user_data)
{
  FmbIconFactory *factory = FMB_ICON_FACTORY (user_data);

  _fmb_return_val_if_fail (FMB_IS_ICON_FACTORY (factory), FALSE);

  GDK_THREADS_ENTER ();

  /* ditch all icons whose ref_count is 1 */
  g_hash_table_foreach_remove (factory->icon_cache, (GHRFunc) fmb_icon_check_sweep, factory);

  GDK_THREADS_LEAVE ();

  return FALSE;
}



static void
fmb_icon_factory_sweep_timer_destroy (gpointer user_data)
{
  FMB_ICON_FACTORY (user_data)->sweep_timer_id = 0;
}



static inline gboolean
thumbnail_needs_frame (const GdkPixbuf *thumbnail,
                       gint             width,
                       gint             height)
{
  const guchar *pixels;
  gint          rowstride;
  gint          n;

  /* don't add frames to small thumbnails */
  if (width < FMB_THUMBNAIL_SIZE && height < FMB_THUMBNAIL_SIZE)
    return FALSE;

  /* always add a frame to thumbnails w/o alpha channel */
  if (G_LIKELY (!gdk_pixbuf_get_has_alpha (thumbnail)))
    return TRUE;

  /* get a pointer to the thumbnail data */
  pixels = gdk_pixbuf_get_pixels (thumbnail);

  /* check if we have a transparent pixel on the first row */
  for (n = width * 4; n > 0; n -= 4)
    if (pixels[n - 1] < 255u)
      return FALSE;

  /* determine the rowstride */
  rowstride = gdk_pixbuf_get_rowstride (thumbnail);

  /* skip the first row */
  pixels += rowstride;

  /* check if we have a transparent pixel in the first or last column */
  for (n = height - 2; n > 0; --n, pixels += rowstride)
    if (pixels[3] < 255u || pixels[width * 4 - 1] < 255u)
      return FALSE;

  /* check if we have a transparent pixel on the last row */
  for (n = width * 4; n > 0; n -= 4)
    if (pixels[n - 1] < 255u)
      return FALSE;

  return TRUE;
}



static GdkPixbuf*
fmb_icon_factory_load_from_file (FmbIconFactory *factory,
                                    const gchar       *path,
                                    gint               size)
{
  GdkPixbuf *pixbuf;
  GdkPixbuf *frame;
  GdkPixbuf *tmp;
  gboolean   needs_frame;
  gint       max_width;
  gint       max_height;
  gint       width;
  gint       height;

  /* try to load the image from the file */
  pixbuf = gdk_pixbuf_new_from_file (path, NULL);
  if (G_LIKELY (pixbuf != NULL))
    {
      /* determine the dimensions of the pixbuf */
      width = gdk_pixbuf_get_width (pixbuf);
      height = gdk_pixbuf_get_height (pixbuf);

      /* check if we want to add a frame to the image (we really don't
       * want to do this for icons displayed in the details view).
       */
      needs_frame = (strstr (path, G_DIR_SEPARATOR_S ".thumbnails" G_DIR_SEPARATOR_S) != NULL)
                 && (size >= 32) && thumbnail_needs_frame (pixbuf, width, height);

      /* be sure to make framed thumbnails fit into the size */
      if (G_LIKELY (needs_frame))
        {
          max_width = size - (3 + 6);
          max_height = size - (3 + 6);
        }
      else
        {
          max_width = size;
          max_height = size;
        }

      /* scale down the icon (if required) */
      if (G_LIKELY (width > max_width || height > max_height))
        {
          /* scale down to the required size */
          tmp = blxo_gdk_pixbuf_scale_down (pixbuf, TRUE, MAX (1, max_height), MAX (1, max_height));
          g_object_unref (G_OBJECT (pixbuf));
          pixbuf = tmp;
        }

      /* add a frame around thumbnail (large) images */
      if (G_LIKELY (needs_frame))
        {
          /* add a frame to the thumbnail */
          frame = gdk_pixbuf_new_from_inline (-1, fmb_thumbnail_frame, FALSE, NULL);
          tmp = blxo_gdk_pixbuf_frame (pixbuf, frame, 4, 3, 5, 6);
          g_object_unref (G_OBJECT (pixbuf));
          g_object_unref (G_OBJECT (frame));
          pixbuf = tmp;
        }
    }

  return pixbuf;
}



static GdkPixbuf*
fmb_icon_factory_lookup_icon (FmbIconFactory *factory,
                                 const gchar       *name,
                                 gint               size,
                                 gboolean           wants_default)
{
  FmbIconKey  lookup_key;
  FmbIconKey *key;
  GtkIconInfo   *icon_info;
  GdkPixbuf     *pixbuf = NULL;

  _fmb_return_val_if_fail (FMB_IS_ICON_FACTORY (factory), NULL);
  _fmb_return_val_if_fail (name != NULL && *name != '\0', NULL);
  _fmb_return_val_if_fail (size > 0, NULL);

  /* prepare the lookup key */
  lookup_key.name = (gchar *) name;
  lookup_key.size = size;

  /* check if we already have a cached version of the icon */
  if (!g_hash_table_lookup_extended (factory->icon_cache, &lookup_key, NULL, (gpointer) &pixbuf))
    {
      /* check if we have to load a file instead of a themed icon */
      if (G_UNLIKELY (g_path_is_absolute (name)))
        {
          /* load the file directly */
          pixbuf = fmb_icon_factory_load_from_file (factory, name, size);
        }
      else
        {
          /* check if the icon theme contains an icon of that name */
          icon_info = gtk_icon_theme_lookup_icon (factory->icon_theme, name, size, 0);
          if (G_LIKELY (icon_info != NULL))
            {
              /* try to load the pixbuf from the icon info */
              pixbuf = gtk_icon_info_load_icon (icon_info, NULL);

              /* cleanup */
              gtk_icon_info_free (icon_info);
            }
        }

      /* use fallback icon if no pixbuf could be loaded */
      if (G_UNLIKELY (pixbuf == NULL))
        {
          /* check if we are allowed to return the fallback icon */
          if (!wants_default)
            return NULL;
          else
            return fmb_icon_factory_load_fallback (factory, size);
        }

      /* generate a key for the new cached icon */
      key = g_slice_new (FmbIconKey);
      key->size = size;
      key->name = g_strdup (name);

      /* insert the new icon into the cache */
      g_hash_table_insert (factory->icon_cache, key, pixbuf);
    }

  /* schedule the sweeper */
  if (G_UNLIKELY (factory->sweep_timer_id == 0))
    {
      factory->sweep_timer_id = g_timeout_add_seconds_full (G_PRIORITY_LOW, FMB_ICON_FACTORY_SWEEP_TIMEOUT,
                                                            fmb_icon_factory_sweep_timer, factory,
                                                            fmb_icon_factory_sweep_timer_destroy);
    }

  return g_object_ref (G_OBJECT (pixbuf));
}



static guint
fmb_icon_key_hash (gconstpointer data)
{
  const FmbIconKey *key = data;
  const gchar         *p;
  guint                h;

  h = (guint) key->size << 5;

  for (p = key->name; *p != '\0'; ++p)
    h = (h << 5) - h + *p;

  return h;
}



static gboolean
fmb_icon_key_equal (gconstpointer a,
                       gconstpointer b)
{
  const FmbIconKey *a_key = a;
  const FmbIconKey *b_key = b;

  /* compare sizes first */
  if (a_key->size != b_key->size)
    return FALSE;

  /* do a full string comparison on the names */
  return blxo_str_is_equal (a_key->name, b_key->name);
}



static void
fmb_icon_key_free (gpointer data)
{
  FmbIconKey *key = data;

  g_free (key->name);
  g_slice_free (FmbIconKey, key);
}



static void
fmb_icon_store_free (gpointer data)
{
  FmbIconStore *store = data;

  if (store->icon != NULL)
    g_object_unref (store->icon);
  g_slice_free (FmbIconStore, store);
}



static GdkPixbuf*
fmb_icon_factory_load_fallback (FmbIconFactory *factory,
                                   gint               size)
{
  return fmb_icon_factory_lookup_icon (factory, "text-x-generic", size, FALSE);
}



/**
 * fmb_icon_factory_get_default:
 *
 * Returns the #FmbIconFactory that operates on the default #GtkIconTheme.
 * The default #FmbIconFactory instance will be around for the time the
 * programs runs, starting with the first call to this function.
 *
 * The caller is responsible to free the returned object using
 * g_object_unref() when no longer needed.
 *
 * Return value: the #FmbIconFactory for the default icon theme.
 **/
FmbIconFactory*
fmb_icon_factory_get_default (void)
{
  static FmbIconFactory *factory = NULL;

  if (G_UNLIKELY (factory == NULL))
    {
      factory = fmb_icon_factory_get_for_icon_theme (gtk_icon_theme_get_default ());
      g_object_add_weak_pointer (G_OBJECT (factory), (gpointer) &factory);
    }
  else
    {
      g_object_ref (G_OBJECT (factory));
    }

  return factory;
}



/**
 * fmb_icon_factory_get_for_icon_theme:
 * @icon_theme : a #GtkIconTheme instance.
 *
 * Determines the proper #FmbIconFactory to be used with the specified
 * @icon_theme and returns it.
 *
 * You need to explicitly free the returned #FmbIconFactory object
 * using g_object_unref() when you are done with it.
 *
 * Return value: the #FmbIconFactory for @icon_theme.
 **/
FmbIconFactory*
fmb_icon_factory_get_for_icon_theme (GtkIconTheme *icon_theme)
{
  FmbIconFactory *factory;

  _fmb_return_val_if_fail (GTK_IS_ICON_THEME (icon_theme), NULL);

  /* generate the quark on-demand */
  if (G_UNLIKELY (fmb_icon_factory_quark == 0))
    fmb_icon_factory_quark = g_quark_from_static_string ("fmb-icon-factory");

  /* check if the given icon theme already knows about an icon factory */
  factory = g_object_get_qdata (G_OBJECT (icon_theme), fmb_icon_factory_quark);
  if (G_UNLIKELY (factory == NULL))
    {
      /* allocate a new factory and connect it to the icon theme */
      factory = g_object_new (FMB_TYPE_ICON_FACTORY, NULL);
      factory->icon_theme = g_object_ref (G_OBJECT (icon_theme));
      g_object_set_qdata (G_OBJECT (factory->icon_theme), fmb_icon_factory_quark, factory);

      /* connect the "show-thumbnails" property to the global preference */
      factory->preferences = fmb_preferences_get ();
      blxo_binding_new (G_OBJECT (factory->preferences), "misc-thumbnail-mode",
                       G_OBJECT (factory), "thumbnail-mode");
    }
  else
    {
      g_object_ref (G_OBJECT (factory));
    }

  return factory;
}



/**
 * fmb_icon_factory_get_thumbnail_mode:
 * @factory       : a #FmbIconFactory instance.
 * @file          : a #FmbFile.
 *
 * Return value: if a Thumbnail show be shown for @file.
 **/
gboolean
fmb_icon_factory_get_show_thumbnail (const FmbIconFactory *factory,
                                        const FmbFile        *file)
{
  GFilesystemPreviewType preview;

  _fmb_return_val_if_fail (FMB_IS_ICON_FACTORY (factory), FMB_THUMBNAIL_MODE_NEVER);
  _fmb_return_val_if_fail (file == NULL || FMB_IS_FILE (file), FMB_THUMBNAIL_MODE_NEVER);

  if (file == NULL
      || factory->thumbnail_mode == FMB_THUMBNAIL_MODE_NEVER)
    return FALSE;

  /* always create thumbs for local files */
  if (fmb_file_is_local (file))
    return TRUE;

  preview = fmb_file_get_preview_type (file);

  /* file system says to never thumbnail anything */
  if (preview == G_FILESYSTEM_PREVIEW_TYPE_NEVER)
    return FALSE;

  /* only if the setting is local and the fs reports to be local */
  if (factory->thumbnail_mode == FMB_THUMBNAIL_MODE_ONLY_LOCAL)
    return preview == G_FILESYSTEM_PREVIEW_TYPE_IF_LOCAL;

  /* FMB_THUMBNAIL_MODE_ALWAYS */
  return TRUE;
}



/**
 * fmb_icon_factory_load_icon:
 * @factory       : a #FmbIconFactory instance.
 * @name          : name of the icon to load.
 * @size          : desired icon size.
 * @wants_default : %TRUE to return the fallback icon if no icon of @name
 *                  is found in the @factory.
 *
 * Looks up the icon named @name in the icon theme associated with @factory
 * and returns a pixbuf for the icon at the given @size. This function will
 * return a default fallback icon if the requested icon could not be found
 * and @wants_default is %TRUE, else %NULL will be returned in that case.
 *
 * Call g_object_unref() on the returned pixbuf when you are
 * done with it.
 *
 * Return value: the pixbuf for the icon named @name at @size.
 **/
GdkPixbuf*
fmb_icon_factory_load_icon (FmbIconFactory        *factory,
                               const gchar              *name,
                               gint                      size,
                               gboolean                  wants_default)
{
  _fmb_return_val_if_fail (FMB_IS_ICON_FACTORY (factory), NULL);
  _fmb_return_val_if_fail (size > 0, NULL);
  
  /* cannot happen unless there's no XSETTINGS manager
   * for the default screen, but just in case...
   */
  if (G_UNLIKELY (blxo_str_is_empty (name)))
    {
      /* check if the caller will happly accept the fallback icon */
      if (G_LIKELY (wants_default))
        return fmb_icon_factory_load_fallback (factory, size);
      else
        return NULL;
    }

  /* lookup the icon */
  return fmb_icon_factory_lookup_icon (factory, name, size, wants_default);
}



/**
 * fmb_icon_factory_load_file_icon:
 * @factory    : a #FmbIconFactory instance.
 * @file       : a #FmbFile.
 * @icon_state : the desired icon state.
 * @icon_size  : the desired icon size.
 *
 * The caller is responsible to free the returned object using
 * g_object_unref() when no longer needed.
 *
 * Return value: the #GdkPixbuf icon.
 **/
GdkPixbuf*
fmb_icon_factory_load_file_icon (FmbIconFactory  *factory,
                                    FmbFile         *file,
                                    FmbFileIconState icon_state,
                                    gint                icon_size)
{
  GInputStream    *stream;
  GtkIconInfo     *icon_info;
  const gchar     *thumbnail_path;
  GdkPixbuf       *icon = NULL;
  GIcon           *gicon;
  const gchar     *icon_name;
  const gchar     *custom_icon;
  FmbIconStore *store;

  _fmb_return_val_if_fail (FMB_IS_ICON_FACTORY (factory), NULL);
  _fmb_return_val_if_fail (FMB_IS_FILE (file), NULL);
  _fmb_return_val_if_fail (icon_size > 0, NULL);

  /* check if we have a stored icon on the file and it is still valid */
  store = g_object_get_qdata (G_OBJECT (file), fmb_icon_factory_store_quark);
  if (store != NULL
      && store->icon_state == icon_state
      && store->icon_size == icon_size
      && store->stamp == factory->theme_stamp
      && store->thumb_state == fmb_file_get_thumb_state (file))
    {
      return g_object_ref (store->icon);
    }

  /* check if we have a custom icon for this file */
  custom_icon = fmb_file_get_custom_icon (file);
  if (custom_icon != NULL)
    {
      /* try to load the icon */
      icon = fmb_icon_factory_lookup_icon (factory, custom_icon, icon_size, FALSE);
      if (G_LIKELY (icon != NULL))
        return icon;
    }

  /* check if thumbnails are enabled and we can display a thumbnail for the item */
  if (fmb_icon_factory_get_show_thumbnail (factory, file)
      && fmb_file_is_regular (file))
    {
      /* determine the preview icon first */
      gicon = fmb_file_get_preview_icon (file);

      /* check if we have a preview icon */
      if (gicon != NULL)
        {
          if (G_IS_THEMED_ICON (gicon))
            {
              /* we have a themed preview icon, look it up using the icon theme */
              icon_info = 
                gtk_icon_theme_lookup_by_gicon (factory->icon_theme, 
                                                gicon, icon_size, 
                                                GTK_ICON_LOOKUP_USE_BUILTIN
                                                | GTK_ICON_LOOKUP_FORCE_SIZE);

              /* check if the lookup succeeded */
              if (icon_info != NULL)
                {
                  /* try to load the pixbuf from the icon info */
                  icon = gtk_icon_info_load_icon (icon_info, NULL);
                  gtk_icon_info_free (icon_info);
                }
            }
          else if (G_IS_LOADABLE_ICON (gicon))
            {
              /* we have a loadable icon, try to open it for reading */
              stream = g_loadable_icon_load (G_LOADABLE_ICON (gicon), icon_size,
                                             NULL, NULL, NULL);

              /* check if we have a valid input stream */
              if (stream != NULL)
                {
                  /* load the pixbuf from the stream */
                  icon = gdk_pixbuf_new_from_stream_at_scale (stream, icon_size,
                                                              icon_size, TRUE,
                                                              NULL, NULL);

                  /* destroy the stream */
                  g_object_unref (stream);
                }
            }

          /* return the icon if we have one */
          if (icon != NULL)
            return icon;
        }
      else
        {
          /* we have no preview icon but the thumbnail should be ready. determine
           * the filename of the thumbnail */
          thumbnail_path = fmb_file_get_thumbnail_path (file);

          /* check if we have a valid path */
          if (thumbnail_path != NULL)
            {
              /* try to load the thumbnail */
              icon = fmb_icon_factory_load_from_file (factory, thumbnail_path, icon_size);
            }
        }
    }

  /* lookup the icon name for the icon in the given state and load the icon */
  if (G_LIKELY (icon == NULL))
    {
      icon_name = fmb_file_get_icon_name (file, icon_state, factory->icon_theme);
      icon = fmb_icon_factory_load_icon (factory, icon_name, icon_size, TRUE);
    }

  if (G_LIKELY (icon != NULL))
    {
      store = g_slice_new (FmbIconStore);
      store->icon_size = icon_size;
      store->icon_state = icon_state;
      store->stamp = factory->theme_stamp;
      store->thumb_state = fmb_file_get_thumb_state (file);
      store->icon = g_object_ref (icon);

      g_object_set_qdata_full (G_OBJECT (file), fmb_icon_factory_store_quark,
                               store, fmb_icon_store_free);
    }

  return icon;
}



/**
 * fmb_icon_factory_clear_pixmap_cache:
 * @file : a #FmbFile.
 *
 * Unset the pixmap cache on a file to force a reload on the next request.
 **/
void
fmb_icon_factory_clear_pixmap_cache (FmbFile *file)
{
  _fmb_return_if_fail (FMB_IS_FILE (file));

  /* unset the data */
  if (fmb_icon_factory_store_quark != 0)
    g_object_set_qdata (G_OBJECT (file), fmb_icon_factory_store_quark, NULL);
}
