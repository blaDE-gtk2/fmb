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

#include <fmbx/fmbx.h>

#include <fmb/fmb-application.h>
#include <fmb/fmb-dialogs.h>
#include <fmb/fmb-dnd.h>
#include <fmb/fmb-gtk-extensions.h>
#include <fmb/fmb-private.h>



static void
dnd_action_selected (GtkWidget     *item,
                     GdkDragAction *dnd_action_return)
{
  *dnd_action_return = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (item), "dnd-action"));
}



/**
 * fmb_dnd_ask:
 * @widget    : the widget on which the drop was performed.
 * @folder    : the #FmbFile to which the @path_list is being dropped.
 * @path_list : the #GFile<!---->s of the files being dropped to @file.
 * @timestamp : the time of the drop event.
 * @actions   : the list of actions supported for the drop.
 *
 * Pops up a menu that asks the user to choose one of the
 * @actions or to cancel the drop. If the user chooses a
 * valid #GdkDragAction from @actions, then this action is
 * returned. Else if the user cancels the drop, 0 will be
 * returned.
 *
 * This method can be used to implement a response to the
 * #GDK_ACTION_ASK action on drops.
 *
 * Return value: the selected #GdkDragAction or 0 to cancel.
 **/
GdkDragAction
fmb_dnd_ask (GtkWidget    *widget,
                FmbFile   *folder,
                GList        *path_list,
                guint         timestamp,
                GdkDragAction dnd_actions)
{
  static const GdkDragAction dnd_action_items[] = { GDK_ACTION_COPY, GDK_ACTION_MOVE, GDK_ACTION_LINK };
  static const gchar        *dnd_action_names[] = { N_ ("_Copy here"), N_ ("_Move here"), N_ ("_Link here") };
  static const gchar        *dnd_action_icons[] = { "stock_folder-copy", "stock_folder-move", NULL };

  FmbxProviderFactory *factory;
  GdkDragAction           dnd_action = 0;
  FmbFile             *file;
  GtkWidget              *window;
  GtkWidget              *image;
  GtkWidget              *menu;
  GtkWidget              *item;
  GList                  *file_list = NULL;
  GList                  *providers = NULL;
  GList                  *actions = NULL;
  GList                  *lp;
  guint                   n;

  _fmb_return_val_if_fail (fmb_file_is_directory (folder), 0);
  _fmb_return_val_if_fail (GTK_IS_WIDGET (widget), 0);

  /* connect to the provider factory */
  factory = fmbx_provider_factory_get_default ();

  /* prepare the popup menu */
  menu = gtk_menu_new ();

  /* append the various items */
  for (n = 0; n < G_N_ELEMENTS (dnd_action_items); ++n)
    if (G_LIKELY ((dnd_actions & dnd_action_items[n]) != 0))
      {
        item = gtk_image_menu_item_new_with_mnemonic (_(dnd_action_names[n]));
        g_object_set_data (G_OBJECT (item), I_("dnd-action"), GUINT_TO_POINTER (dnd_action_items[n]));
        g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (dnd_action_selected), &dnd_action);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        gtk_widget_show (item);

        /* add image to the menu item */
        if (G_LIKELY (dnd_action_icons[n] != NULL))
          {
            image = gtk_image_new_from_icon_name (dnd_action_icons[n], GTK_ICON_SIZE_MENU);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
          }
      }

  /* append the separator */
  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* determine the toplevel window the widget belongs to */
  window = gtk_widget_get_toplevel (widget);
  if (G_LIKELY (window != NULL && gtk_widget_get_toplevel (window)))
    {
      /* check if we can resolve all paths */
      for (lp = path_list; lp != NULL; lp = lp->next)
        {
          /* try to resolve this path */
          file = fmb_file_cache_lookup (lp->data);
          if (G_LIKELY (file != NULL))
            file_list = g_list_prepend (file_list, file);
          else
            break;
        }

      /* check if we resolved all paths (and have atleast one file) */
      if (G_LIKELY (file_list != NULL && lp == NULL))
        {
          /* load the menu providers from the provider factory */
          providers = fmbx_provider_factory_list_providers (factory, FMBX_TYPE_MENU_PROVIDER);

          /* load the dnd actions offered by the menu providers */
          for (lp = providers; lp != NULL; lp = lp->next)
            {
              /* merge the actions from this provider */
              actions = g_list_concat (actions, fmbx_menu_provider_get_dnd_actions (lp->data, window, FMBX_FILE_INFO (folder), file_list));
              g_object_unref (G_OBJECT (lp->data));
            }
          g_list_free (providers);

          /* check if we have atleast one action */
          if (G_UNLIKELY (actions != NULL))
            {
              /* add menu items for all actions */
              for (lp = actions; lp != NULL; lp = lp->next)
                {
                  /* add a menu item for the action */
                  item = gtk_action_create_menu_item (lp->data);
                  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
                  g_object_unref (G_OBJECT (lp->data));
                  gtk_widget_show (item);
                }
              g_list_free (actions);

              /* append another separator */
              item = gtk_separator_menu_item_new ();
              gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
              gtk_widget_show (item);
            }
        }
    }

  /* append the cancel item */
  item = gtk_image_menu_item_new_from_stock (GTK_STOCK_CANCEL, NULL);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* run the menu on the widget's screen (takes over the floating reference of menu) */
  fmb_gtk_menu_run (GTK_MENU (menu), widget, NULL, NULL, 3, timestamp);

  /* cleanup */
  g_object_unref (G_OBJECT (factory));
  g_list_free_full (file_list, g_object_unref);

  return dnd_action;
}



/**
 * fmb_dnd_perform:
 * @widget            : the #GtkWidget on which the drop was done.
 * @file              : the #FmbFile on which the @file_list was dropped.
 * @file_list         : the list of #GFile<!---->s that was dropped.
 * @action            : the #GdkDragAction that was performed.
 * @new_files_closure : a #GClosure to connect to the job's "new-files" signal,
 *                      which will be emitted when the job finishes with the
 *                      list of #GFile<!---->s created by the job, or
 *                      %NULL if you're not interested in the signal.
 *
 * Performs the drop of @file_list on @file in @widget, as given in
 * @action and returns %TRUE if the drop was started successfully
 * (or even completed successfully), else %FALSE.
 *
 * Return value: %TRUE if the DnD operation was started
 *               successfully, else %FALSE.
 **/
gboolean
fmb_dnd_perform (GtkWidget    *widget,
                    FmbFile   *file,
                    GList        *file_list,
                    GdkDragAction action,
                    GClosure     *new_files_closure)
{
  FmbApplication *application;
  gboolean           succeed = TRUE;
  GError            *error = NULL;

  _fmb_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  _fmb_return_val_if_fail (FMB_IS_FILE (file), FALSE);
  _fmb_return_val_if_fail (gtk_widget_get_realized (widget), FALSE);

  /* query a reference on the application object */
  application = fmb_application_get ();

  /* check if the file is a directory */
  if (fmb_file_is_directory (file))
    {
      /* perform the given directory operation */
      switch (action)
        {
        case GDK_ACTION_COPY:
          fmb_application_copy_into (application, widget, file_list, fmb_file_get_file (file), new_files_closure);
          break;

        case GDK_ACTION_MOVE:
          fmb_application_move_into (application, widget, file_list, fmb_file_get_file (file), new_files_closure);
          break;

        case GDK_ACTION_LINK:
          fmb_application_link_into (application, widget, file_list, fmb_file_get_file (file), new_files_closure);
          break;

        default:
          succeed = FALSE;
        }
    }
  else if (fmb_file_is_executable (file))
    {
      /* TODO any chance to determine the working dir here? */
      succeed = fmb_file_execute (file, NULL, widget, file_list, NULL, &error);
      if (G_UNLIKELY (!succeed))
        {
          /* display an error to the user */
          fmb_dialogs_show_error (widget, error, _("Failed to execute file \"%s\""), fmb_file_get_display_name (file));

          /* release the error */
          g_error_free (error);
        }
    }
  else
    {
      succeed = FALSE;
    }

  /* release the application reference */
  g_object_unref (G_OBJECT (application));

  return succeed;
}



