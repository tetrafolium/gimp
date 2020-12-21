/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimpproceduredialog.c
 * Copyright (C) 2019 Michael Natterer <mitch@gimp.org>
 * Copyright (C) 2020 Jehan
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

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpwidgets/gimpwidgets.h"

#include "gimp.h"
#include "gimpui.h"

#include "gimpprocedureconfig-private.h"

#include "libgimp-intl.h"


enum
{
    PROP_0,
    PROP_PROCEDURE,
    PROP_CONFIG,
    N_PROPS
};

#define RESPONSE_RESET 1


struct _GimpProcedureDialogPrivate
{
    GimpProcedure       *procedure;
    GimpProcedureConfig *config;
    GimpProcedureConfig *initial_config;

    GtkWidget           *reset_popover;

    GHashTable          *widgets;
    GHashTable          *mnemonics;
    GHashTable          *core_mnemonics;
    GtkSizeGroup        *label_group;
};


static void   gimp_procedure_dialog_constructed   (GObject      *object);
static void   gimp_procedure_dialog_dispose       (GObject      *object);
static void   gimp_procedure_dialog_set_property  (GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec);
static void   gimp_procedure_dialog_get_property  (GObject      *object,
        guint         property_id,
        GValue       *value,
        GParamSpec   *pspec);

static void  gimp_procedure_dialog_real_fill_list (GimpProcedureDialog *dialog,
        GimpProcedure       *procedure,
        GimpProcedureConfig *config,
        GList               *properties);

static void   gimp_procedure_dialog_reset_initial (GtkWidget           *button,
        GimpProcedureDialog *dialog);
static void   gimp_procedure_dialog_reset_factory (GtkWidget           *button,
        GimpProcedureDialog *dialog);
static void   gimp_procedure_dialog_load_defaults (GtkWidget           *button,
        GimpProcedureDialog *dialog);
static void   gimp_procedure_dialog_save_defaults (GtkWidget           *button,
        GimpProcedureDialog *dialog);

static gboolean gimp_procedure_dialog_check_mnemonic    (GimpProcedureDialog *dialog,
        GtkWidget           *widget,
        const gchar         *id,
        const gchar         *core_id);
static GtkWidget *
gimp_procedure_dialog_fill_container_list (GimpProcedureDialog *dialog,
        const gchar         *container_id,
        GtkContainer        *container,
        GList               *properties);

G_DEFINE_TYPE_WITH_PRIVATE (GimpProcedureDialog, gimp_procedure_dialog,
                            GIMP_TYPE_DIALOG)

#define parent_class gimp_procedure_dialog_parent_class

static GParamSpec *props[N_PROPS] = { NULL, };


static void
gimp_procedure_dialog_class_init (GimpProcedureDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed  = gimp_procedure_dialog_constructed;
    object_class->dispose      = gimp_procedure_dialog_dispose;
    object_class->get_property = gimp_procedure_dialog_get_property;
    object_class->set_property = gimp_procedure_dialog_set_property;

    klass->fill_list           = gimp_procedure_dialog_real_fill_list;

    props[PROP_PROCEDURE] =
        g_param_spec_object ("procedure",
                             "Procedure",
                             "The GimpProcedure this dialog is used with",
                             GIMP_TYPE_PROCEDURE,
                             GIMP_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT);

    props[PROP_CONFIG] =
        g_param_spec_object ("config",
                             "Config",
                             "The GimpProcedureConfig this dialog is editing",
                             GIMP_TYPE_PROCEDURE_CONFIG,
                             GIMP_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT);

    g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
gimp_procedure_dialog_init (GimpProcedureDialog *dialog)
{
    dialog->priv = gimp_procedure_dialog_get_instance_private (dialog);

    dialog->priv->widgets     = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    dialog->priv->mnemonics   = g_hash_table_new_full (g_direct_hash, NULL, NULL, g_free);
    dialog->priv->core_mnemonics = g_hash_table_new_full (g_direct_hash, NULL, NULL, g_free);
    dialog->priv->label_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

static void
gimp_procedure_dialog_constructed (GObject *object)
{
    GimpProcedureDialog *dialog;
    GimpProcedure       *procedure;
    const gchar         *ok_label;
    GtkWidget           *hbox;
    GtkWidget           *button;
    GtkWidget           *content_area;
    gchar               *role;

    G_OBJECT_CLASS (parent_class)->constructed (object);

    dialog = GIMP_PROCEDURE_DIALOG (object);
    procedure = dialog->priv->procedure;

    role = g_strdup_printf ("gimp-%s", gimp_procedure_get_name (procedure));
    g_object_set (object,
                  "role", role,
                  NULL);
    g_free (role);

    if (GIMP_IS_LOAD_PROCEDURE (procedure))
        ok_label = _("_Open");
    else if (GIMP_IS_SAVE_PROCEDURE (procedure))
        ok_label = _("_Export");
    else
        ok_label = _("_OK");

    button = gimp_dialog_add_button (GIMP_DIALOG (dialog),
                                     _("_Reset"), RESPONSE_RESET);
    gimp_procedure_dialog_check_mnemonic (GIMP_PROCEDURE_DIALOG (dialog), button, NULL, "reset");
    button = gimp_dialog_add_button (GIMP_DIALOG (dialog),
                                     _("_Cancel"), GTK_RESPONSE_CANCEL);
    gimp_procedure_dialog_check_mnemonic (GIMP_PROCEDURE_DIALOG (dialog), button, NULL, "cancel");
    button = gimp_dialog_add_button (GIMP_DIALOG (dialog),
                                     ok_label, GTK_RESPONSE_OK);
    gimp_procedure_dialog_check_mnemonic (GIMP_PROCEDURE_DIALOG (dialog), button, NULL, "ok");
    /* OK button is the default action and has focus from start.
     * This allows to just accept quickly whatever default values.
     */
    gtk_widget_set_can_default (button, TRUE);
    gtk_widget_grab_focus (button);
    gtk_widget_grab_default (button);

    gimp_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
            GTK_RESPONSE_OK,
            RESPONSE_RESET,
            GTK_RESPONSE_CANCEL,
            -1);

    gimp_window_set_transient (GTK_WINDOW (dialog));

    /* Main content area. */
    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_container_set_border_width (GTK_CONTAINER (content_area), 12);
    gtk_box_set_spacing (GTK_BOX (content_area), 3);

    /* Bottom box buttons with small additional padding. */
    hbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing (GTK_BOX (hbox), 6);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (hbox), GTK_BUTTONBOX_START);
    gtk_box_pack_end (GTK_BOX (content_area), hbox, FALSE, FALSE, 0);
    gtk_container_child_set (GTK_CONTAINER (content_area), hbox,
                             "padding", 3, NULL);
    gtk_widget_show (hbox);

    button = gtk_button_new_with_mnemonic (_("_Load Defaults"));
    gimp_procedure_dialog_check_mnemonic (GIMP_PROCEDURE_DIALOG (dialog), button, NULL, "load-defaults");
    gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
    gtk_widget_show (button);

    g_signal_connect (button, "clicked",
                      G_CALLBACK (gimp_procedure_dialog_load_defaults),
                      dialog);

    button = gtk_button_new_with_mnemonic (_("_Save Defaults"));
    gimp_procedure_dialog_check_mnemonic (GIMP_PROCEDURE_DIALOG (dialog), button, NULL, "save-defaults");
    gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
    gtk_widget_show (button);

    g_signal_connect (button, "clicked",
                      G_CALLBACK (gimp_procedure_dialog_save_defaults),
                      dialog);
}

static void
gimp_procedure_dialog_dispose (GObject *object)
{
    GimpProcedureDialog *dialog = GIMP_PROCEDURE_DIALOG (object);

    g_clear_object (&dialog->priv->procedure);
    g_clear_object (&dialog->priv->config);
    g_clear_object (&dialog->priv->initial_config);

    g_clear_pointer (&dialog->priv->reset_popover, gtk_widget_destroy);

    g_clear_pointer (&dialog->priv->widgets, g_hash_table_destroy);
    g_clear_pointer (&dialog->priv->mnemonics, g_hash_table_destroy);
    g_clear_pointer (&dialog->priv->core_mnemonics, g_hash_table_destroy);

    g_clear_object (&dialog->priv->label_group);

    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gimp_procedure_dialog_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    GimpProcedureDialog *dialog = GIMP_PROCEDURE_DIALOG (object);

    switch (property_id)
    {
    case PROP_PROCEDURE:
        dialog->priv->procedure = g_value_dup_object (value);
        break;

    case PROP_CONFIG:
        dialog->priv->config = g_value_dup_object (value);

        if (dialog->priv->config)
            dialog->priv->initial_config =
                gimp_config_duplicate (GIMP_CONFIG (dialog->priv->config));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gimp_procedure_dialog_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    GimpProcedureDialog *dialog = GIMP_PROCEDURE_DIALOG (object);

    switch (property_id)
    {
    case PROP_PROCEDURE:
        g_value_set_object (value, dialog->priv->procedure);
        break;

    case PROP_CONFIG:
        g_value_set_object (value, dialog->priv->config);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gimp_procedure_dialog_real_fill_list (GimpProcedureDialog *dialog,
                                      GimpProcedure       *procedure,
                                      GimpProcedureConfig *config,
                                      GList               *properties)
{
    GtkWidget *content_area;
    GList     *iter;

    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

    for (iter = properties; iter; iter = iter->next)
    {
        GtkWidget *widget;

        widget = gimp_procedure_dialog_get_widget (dialog, iter->data, G_TYPE_NONE);
        if (widget)
        {
            /* Reference the widget because the hash table will
             * unreference it anyway when getting destroyed so we don't
             * want to give the only reference to the parent widget.
             */
            g_object_ref (widget);
            gtk_box_pack_start (GTK_BOX (content_area), widget, TRUE, TRUE, 0);
            gtk_widget_show (widget);
        }
    }
}

GtkWidget *
gimp_procedure_dialog_new (GimpProcedure       *procedure,
                           GimpProcedureConfig *config,
                           const gchar         *title)
{
    GtkWidget   *dialog;
    const gchar *help_id;
    gboolean     use_header_bar;

    g_return_val_if_fail (GIMP_IS_PROCEDURE (procedure), NULL);
    g_return_val_if_fail (GIMP_IS_PROCEDURE_CONFIG (config), NULL);
    g_return_val_if_fail (gimp_procedure_config_get_procedure (config) ==
                          procedure, NULL);
    g_return_val_if_fail (title != NULL, NULL);

    help_id = gimp_procedure_get_help_id (procedure);

    g_object_get (gtk_settings_get_default (),
                  "gtk-dialogs-use-header", &use_header_bar,
                  NULL);

    dialog = g_object_new (GIMP_TYPE_PROCEDURE_DIALOG,
                           "procedure",      procedure,
                           "config",         config,
                           "title",          title,
                           "help-func",      gimp_standard_help_func,
                           "help-id",        help_id,
                           "use-header-bar", use_header_bar,
                           NULL);

    return GTK_WIDGET (dialog);
}

/**
 * gimp_procedure_dialog_get_widget:
 * @dialog:      the associated #GimpProcedureDialog.
 * @property:    name of the property to build a widget for. It must be
 *               a property of the #GimpProcedure @dialog has been
 *               created for.
 * @widget_type: alternative widget type. %G_TYPE_NONE will create the
 *               default type of widget for the associated property
 *               type.
 *
 * Creates a new #GtkWidget for @property according to the property
 * type. The following types are possible:
 *
 * - %G_TYPE_PARAM_BOOLEAN: %GTK_TYPE_CHECK_BUTTON (default) or
 *   %GTK_TYPE_SWITCH
 * - %G_TYPE_PARAM_INT: %GIMP_TYPE_LABEL_SPIN (default) or
 *   %GIMP_TYPE_SCALE_ENTRY or %GIMP_TYPE_SPIN_BUTTON (no label).
 * - %G_TYPE_PARAM_STRING: %GTK_TYPE_ENTRY (default).
 *
 * If the @widget_type is not supported for the actual type of
 * @property, the function will fail. To keep the default, set to
 * %G_TYPE_NONE.
 *
 * If a widget has already been created for this procedure, it will be
 * returned instead (even if with a different @widget_type).
 *
 * Returns: (transfer none): the #GtkWidget representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_get_widget (GimpProcedureDialog *dialog,
                                  const gchar         *property,
                                  GType                widget_type)
{
    GtkWidget  *widget = NULL;
    GtkWidget  *label  = NULL;
    GParamSpec *pspec;

    g_return_val_if_fail (property != NULL, NULL);

    /* First check if it already exists. */
    widget = g_hash_table_lookup (dialog->priv->widgets, property);

    if (widget)
        return widget;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->priv->config),
                                          property);
    if (! pspec)
    {
        g_warning ("%s: parameter %s does not exist.",
                   G_STRFUNC, property);
        return NULL;
    }

    if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_BOOLEAN)
    {
        if (widget_type == G_TYPE_NONE || widget_type == GTK_TYPE_CHECK_BUTTON)
            widget = gimp_prop_check_button_new (G_OBJECT (dialog->priv->config),
                                                 property,
                                                 _(g_param_spec_get_nick (pspec)));
        else if (widget_type == GTK_TYPE_SWITCH)
            widget = gimp_prop_switch_new (G_OBJECT (dialog->priv->config),
                                           property,
                                           _(g_param_spec_get_nick (pspec)),
                                           &label, NULL);
    }
    else if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_INT ||
             G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_DOUBLE)
    {
        gdouble minimum;
        gdouble maximum;
        gdouble step   = 0.0;
        gdouble page   = 0.0;
        gint    digits = 0;

        if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_INT)
        {
            GParamSpecInt *pspecint = (GParamSpecInt *) pspec;

            minimum = (gdouble) pspecint->minimum;
            maximum = (gdouble) pspecint->maximum;
        }
        else /* G_TYPE_PARAM_DOUBLE */
        {
            GParamSpecDouble *pspecdouble = (GParamSpecDouble *) pspec;

            minimum = pspecdouble->minimum;
            maximum = pspecdouble->maximum;
        }
        gimp_range_estimate_settings (minimum, maximum, &step, &page, &digits);

        if (widget_type == G_TYPE_NONE || widget_type == GIMP_TYPE_LABEL_SPIN)
        {
            widget = gimp_prop_label_spin_new (G_OBJECT (dialog->priv->config),
                                               property, digits);
        }
        else if (widget_type == GIMP_TYPE_SCALE_ENTRY)
        {
            widget = gimp_prop_scale_entry_new (G_OBJECT (dialog->priv->config),
                                                property,
                                                _(g_param_spec_get_nick (pspec)),
                                                1.0, FALSE, 0.0, 0.0);
        }
        else if (widget_type == GIMP_TYPE_SPIN_BUTTON)
        {
            /* Just some spin button without label. */
            widget = gimp_prop_spin_button_new (G_OBJECT (dialog->priv->config),
                                                property, step, page, digits);
        }
    }
    else if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_STRING)
    {
        if (widget_type == G_TYPE_NONE || widget_type == GTK_TYPE_TEXT_VIEW)
        {
            GtkTextBuffer *buffer;
            const gchar   *tooltip;

            buffer = gimp_prop_text_buffer_new (G_OBJECT (dialog->priv->config),
                                                property, -1);
            widget = gtk_text_view_new_with_buffer (buffer);
            gtk_text_view_set_top_margin (GTK_TEXT_VIEW (widget), 3);
            gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (widget), 3);
            gtk_text_view_set_left_margin (GTK_TEXT_VIEW (widget), 3);
            gtk_text_view_set_right_margin (GTK_TEXT_VIEW (widget), 3);
            g_object_unref (buffer);

            tooltip = g_param_spec_get_blurb (pspec);
            if (tooltip)
                gimp_help_set_help_data (widget, tooltip, NULL);
        }
        else if (widget_type == GTK_TYPE_ENTRY)
        {
            widget = gimp_prop_entry_new (G_OBJECT (dialog->priv->config),
                                          property, -1);
        }
    }
    else
    {
        g_warning ("%s: parameter %s has non supported type %s",
                   G_STRFUNC, property, G_PARAM_SPEC_TYPE_NAME (pspec));
        return NULL;
    }

    if (! widget)
    {
        g_warning ("%s: widget type %s not supported for parameter '%s' of type %s",
                   G_STRFUNC, g_type_name (widget_type),
                   property, G_PARAM_SPEC_TYPE_NAME (pspec));
        return NULL;
    }
    else if (GIMP_IS_LABELED (widget) || label)
    {
        if (! label)
            label = gimp_labeled_get_label (GIMP_LABELED (widget));

        gtk_size_group_add_widget (dialog->priv->label_group, label);
    }

    gimp_procedure_dialog_check_mnemonic (dialog, widget, property, NULL);
    g_hash_table_insert (dialog->priv->widgets, g_strdup (property), widget);

    return widget;
}

/**
 * gimp_procedure_dialog_get_int_combo:
 * @dialog:   the associated #GimpProcedureDialog.
 * @property: name of the int property to build a combo for. It must be
 *            a property of the #GimpProcedure @dialog has been created
 *            for.
 * @store:    the #GimpIntStore which will be used by the combo box.
 *
 * Creates a new #GimpLabelIntWidget for @property which must
 * necessarily be an integer or boolean property.
 * This must be used instead of gimp_procedure_dialog_get_widget() when
 * you want to create a combo box from an integer property.
 *
 * If a widget has already been created for this procedure, it will be
 * returned instead (whatever its actual widget type).
 *
 * Returns: (transfer none): the #GtkWidget representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_get_int_combo (GimpProcedureDialog *dialog,
                                     const gchar         *property,
                                     GimpIntStore        *store)
{
    GtkWidget  *widget = NULL;
    GParamSpec *pspec;

    g_return_val_if_fail (property != NULL, NULL);

    /* First check if it already exists. */
    widget = g_hash_table_lookup (dialog->priv->widgets, property);

    if (widget)
        return widget;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->priv->config),
                                          property);
    if (! pspec)
    {
        g_warning ("%s: parameter %s does not exist.",
                   G_STRFUNC, property);
        return NULL;
    }

    if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_BOOLEAN ||
            G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_INT)
    {
        widget = gimp_prop_int_combo_box_new (G_OBJECT (dialog->priv->config),
                                              property, store);
        gtk_widget_set_vexpand (widget, FALSE);
        gtk_widget_set_hexpand (widget, TRUE);
        widget = gimp_label_int_widget_new (_(g_param_spec_get_nick (pspec)),
                                            widget);
    }

    if (! widget)
    {
        g_warning ("%s: parameter '%s' of type %s not suitable as GimpIntComboBox",
                   G_STRFUNC, property, G_PARAM_SPEC_TYPE_NAME (pspec));
        return NULL;
    }
    else if (GIMP_IS_LABELED (widget))
    {
        GtkWidget *label = gimp_labeled_get_label (GIMP_LABELED (widget));

        gtk_size_group_add_widget (dialog->priv->label_group, label);
    }

    gimp_procedure_dialog_check_mnemonic (dialog, widget, property, NULL);
    g_hash_table_insert (dialog->priv->widgets, g_strdup (property), widget);

    return widget;
}

/**
 * gimp_procedure_dialog_get_scale_entry:
 * @dialog:   the associated #GimpProcedureDialog.
 * @property: name of the int property to build a combo for. It must be
 *            a property of the #GimpProcedure @dialog has been created
 *            for.
 * @factor:   a display factor for the range shown by the widget.
 *
 * Creates a new #GimpScaleEntry for @property which must necessarily be
 * an integer or double property.
 * This can be used instead of gimp_procedure_dialog_get_widget() in
 * particular if you want to tweak the display factor. A typical example
 * is showing a [0.0, 1.0] range as [0.0, 100.0] instead (@factor = 100.0).
 *
 * If a widget has already been created for this procedure, it will be
 * returned instead (whatever its actual widget type).
 *
 * Returns: (transfer none): the #GtkWidget representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_get_scale_entry (GimpProcedureDialog *dialog,
                                       const gchar         *property,
                                       gdouble              factor)
{
    GtkWidget  *widget = NULL;
    GParamSpec *pspec;

    g_return_val_if_fail (GIMP_IS_PROCEDURE_DIALOG (dialog), NULL);
    g_return_val_if_fail (property != NULL, NULL);

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->priv->config),
                                          property);

    if (! pspec)
    {
        g_warning ("%s: parameter %s does not exist.",
                   G_STRFUNC, property);
        return NULL;
    }

    g_return_val_if_fail (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_INT ||
                          G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_DOUBLE, NULL);

    /* First check if it already exists. */
    widget = g_hash_table_lookup (dialog->priv->widgets, property);

    if (widget)
        return widget;

    widget = gimp_prop_scale_entry_new (G_OBJECT (dialog->priv->config),
                                        property,
                                        _(g_param_spec_get_nick (pspec)),
                                        factor, FALSE, 0.0, 0.0);

    gtk_size_group_add_widget (dialog->priv->label_group,
                               gimp_labeled_get_label (GIMP_LABELED (widget)));

    gimp_procedure_dialog_check_mnemonic (dialog, widget, property, NULL);
    g_hash_table_insert (dialog->priv->widgets, g_strdup (property), widget);

    return widget;
}

/**
 * gimp_procedure_dialog_get_label:
 * @dialog:   the #GimpProcedureDialog.
 * @label_id: the label for the #GtkLabel.
 * @text:     the text for the label.
 *
 * Creates a new #GtkLabel with @text. It can be useful for packing
 * textual information in between property settings.
 *
 * The @label_id must be a unique ID which is neither the name of a
 * property of the #GimpProcedureConfig associated to @dialog, nor is it
 * the ID of any previously created label or container. This ID can
 * later be used together with property names to be packed in other
 * containers or inside @dialog itself.
 *
 * Returns: (transfer none): the #GtkWidget representing @label_id. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_get_label (GimpProcedureDialog *dialog,
                                 const gchar         *label_id,
                                 const gchar         *text)
{
    GtkWidget *label;

    g_return_val_if_fail (label_id != NULL, NULL);

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->priv->config),
                                      label_id))
    {
        g_warning ("%s: label identifier '%s' cannot be an existing property name.",
                   G_STRFUNC, label_id);
        return NULL;
    }

    if ((label = g_hash_table_lookup (dialog->priv->widgets, label_id)))
    {
        g_warning ("%s: label identifier '%s' was already configured.",
                   G_STRFUNC, label_id);
        return label;
    }

    label = gtk_label_new (text);
    g_hash_table_insert (dialog->priv->widgets, g_strdup (label_id), label);

    return label;
}

/**
 * gimp_procedure_dialog_fill:
 * @dialog: the #GimpProcedureDialog.
 * @first_property: the first property name.
 * @...: a %NULL-terminated list of other property names.
 *
 * Populate @dialog with the widgets corresponding to every listed
 * properties. If the list is empty, @dialog will be filled by the whole
 * list of properties of the associated #GimpProcedure, in the defined
 * order:
 * |[<!-- language="C" -->
 * gimp_procedure_dialog_fill (dialog, NULL);
 * ]|
 * Nevertheless if you only wish to display a partial list of
 * properties, or if you wish to change the display order, then you have
 * to give an explicit list:
 * |[<!-- language="C" -->
 * gimp_procedure_dialog_fill (dialog, "property-1", "property-2", NULL);
 * ]|
 *
 * Note: you do not have to call gimp_procedure_dialog_get_widget() on
 * every property before calling this function unless you want a given
 * property to be represented by an alternative widget type. By default,
 * each property will get a default representation according to its
 * type.
 */
void
gimp_procedure_dialog_fill (GimpProcedureDialog *dialog,
                            const gchar         *first_property,
                            ...)
{
    const gchar *prop_name = first_property;
    GList       *list      = NULL;
    va_list      va_args;

    g_return_if_fail (GIMP_IS_PROCEDURE_DIALOG (dialog));

    if (first_property)
    {
        va_start (va_args, first_property);

        do
            list = g_list_prepend (list, (gpointer) prop_name);
        while ((prop_name = va_arg (va_args, const gchar *)));

        va_end (va_args);
    }

    list = g_list_reverse (list);
    gimp_procedure_dialog_fill_list (dialog, list);
    if (list)
        g_list_free (list);
}

/**
 * gimp_procedure_dialog_fill_list: (rename-to gimp_procedure_dialog_fill)
 * @dialog: the #GimpProcedureDialog.
 * @properties: (nullable) (element-type gchar*): the list of property names.
 *
 * Populate @dialog with the widgets corresponding to every listed
 * properties. If the list is %NULL, @dialog will be filled by the whole
 * list of properties of the associated #GimpProcedure, in the defined
 * order:
 * |[<!-- language="C" -->
 * gimp_procedure_dialog_fill_list (dialog, NULL);
 * ]|
 * Nevertheless if you only wish to display a partial list of
 * properties, or if you wish to change the display order, then you have
 * to give an explicit list:
 * |[<!-- language="C" -->
 * gimp_procedure_dialog_fill (dialog, "property-1", "property-2", NULL);
 * ]|
 *
 * Note: you do not have to call gimp_procedure_dialog_get_widget() on
 * every property before calling this function unless you want a given
 * property to be represented by an alternative widget type. By default,
 * each property will get a default representation according to its
 * type.
 */
void
gimp_procedure_dialog_fill_list (GimpProcedureDialog *dialog,
                                 GList               *properties)
{
    gboolean free_properties = FALSE;

    if (! properties)
    {
        GParamSpec **pspecs;
        guint        n_pspecs;
        gint         i;

        pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (dialog->priv->config),
                 &n_pspecs);

        for (i = 0; i < n_pspecs; i++)
        {
            const gchar *prop_name;
            GParamSpec  *pspec = pspecs[i];

            /*  skip our own properties  */
            if (pspec->owner_type == GIMP_TYPE_PROCEDURE_CONFIG)
                continue;

            prop_name  = g_param_spec_get_name (pspec);
            properties = g_list_prepend (properties, (gpointer) prop_name);
        }

        properties = g_list_reverse (properties);

        if (properties)
            free_properties = TRUE;
    }

    GIMP_PROCEDURE_DIALOG_GET_CLASS (dialog)->fill_list (dialog,
            dialog->priv->procedure,
            dialog->priv->config,
            properties);

    if (free_properties)
        g_list_free (properties);
}

/**
 * gimp_procedure_dialog_fill_box:
 * @dialog:         the #GimpProcedureDialog.
 * @container_id:   a container identifier.
 * @first_property: the first property name.
 * @...:            a %NULL-terminated list of other property names.
 *
 * Creates and populates a new #GtkBox with widgets corresponding to
 * every listed properties. If the list is empty, the created box will
 * be filled by the whole list of properties of the associated
 * #GimpProcedure, in the defined order. This is similar of how
 * gimp_procedure_dialog_fill() works except that it creates a new
 * widget which is not inside @dialog itself.
 *
 * The @container_id must be a unique ID which is neither the name of a
 * property of the #GimpProcedureConfig associated to @dialog, nor is it
 * the ID of any previously created container. This ID can later be used
 * together with property names to be packed in other containers or
 * inside @dialog itself.
 *
 * Returns: (transfer none): the #GtkBox representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_fill_box (GimpProcedureDialog *dialog,
                                const gchar         *container_id,
                                const gchar         *first_property,
                                ...)
{
    const gchar *prop_name = first_property;
    GtkWidget   *box;
    GList       *list      = NULL;
    va_list      va_args;

    g_return_val_if_fail (GIMP_IS_PROCEDURE_DIALOG (dialog), NULL);
    g_return_val_if_fail (container_id != NULL, NULL);

    if (first_property)
    {
        va_start (va_args, first_property);

        do
            list = g_list_prepend (list, (gpointer) prop_name);
        while ((prop_name = va_arg (va_args, const gchar *)));

        va_end (va_args);
    }

    list = g_list_reverse (list);
    box = gimp_procedure_dialog_fill_box_list (dialog, container_id, list);
    if (list)
        g_list_free (list);

    return box;
}

/**
 * gimp_procedure_dialog_fill_box_list: (rename-to gimp_procedure_dialog_fill_box)
 * @dialog:        the #GimpProcedureDialog.
 * @container_id:  a container identifier.
 * @properties: (nullable) (element-type gchar*): the list of property names.
 *
 * Creates and populates a new #GtkBox with widgets corresponding to
 * every listed @properties. If the list is empty, the created box will
 * be filled by the whole list of properties of the associated
 * #GimpProcedure, in the defined order. This is similar of how
 * gimp_procedure_dialog_fill() works except that it creates a new
 * widget which is not inside @dialog itself.
 *
 * The @container_id must be a unique ID which is neither the name of a
 * property of the #GimpProcedureConfig associated to @dialog, nor is it
 * the ID of any previously created container. This ID can later be used
 * together with property names to be packed in other containers or
 * inside @dialog itself.
 *
 * Returns: (transfer none): the #GtkBox representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_fill_box_list (GimpProcedureDialog *dialog,
                                     const gchar         *container_id,
                                     GList               *properties)
{
    g_return_val_if_fail (container_id != NULL, NULL);

    return gimp_procedure_dialog_fill_container_list (dialog, container_id,
            GTK_CONTAINER (gtk_box_new (GTK_ORIENTATION_VERTICAL, 2)),
            properties);
}

/**
 * gimp_procedure_dialog_fill_flowbox:
 * @dialog:         the #GimpProcedureDialog.
 * @container_id:   a container identifier.
 * @first_property: the first property name.
 * @...:            a %NULL-terminated list of other property names.
 *
 * Creates and populates a new #GtkFlowBox with widgets corresponding to
 * every listed properties. If the list is empty, the created flowbox
 * will be filled by the whole list of properties of the associated
 * #GimpProcedure, in the defined order. This is similar of how
 * gimp_procedure_dialog_fill() works except that it creates a new
 * widget which is not inside @dialog itself.
 *
 * The @container_id must be a unique ID which is neither the name of a
 * property of the #GimpProcedureConfig associated to @dialog, nor is it
 * the ID of any previously created container. This ID can later be used
 * together with property names to be packed in other containers or
 * inside @dialog itself.
 *
 * Returns: (transfer none): the #GtkFlowBox representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_fill_flowbox (GimpProcedureDialog *dialog,
                                    const gchar         *container_id,
                                    const gchar         *first_property,
                                    ...)
{
    const gchar *prop_name = first_property;
    GtkWidget   *flowbox;
    GList       *list      = NULL;
    va_list      va_args;

    g_return_val_if_fail (GIMP_IS_PROCEDURE_DIALOG (dialog), NULL);
    g_return_val_if_fail (container_id != NULL, NULL);

    if (first_property)
    {
        va_start (va_args, first_property);

        do
            list = g_list_prepend (list, (gpointer) prop_name);
        while ((prop_name = va_arg (va_args, const gchar *)));

        va_end (va_args);
    }

    list = g_list_reverse (list);
    flowbox = gimp_procedure_dialog_fill_flowbox_list (dialog, container_id, list);
    if (list)
        g_list_free (list);

    return flowbox;
}

/**
 * gimp_procedure_dialog_fill_flowbox_list: (rename-to gimp_procedure_dialog_fill_flowbox)
 * @dialog:        the #GimpProcedureDialog.
 * @container_id:  a container identifier.
 * @properties: (nullable) (element-type gchar*): the list of property names.
 *
 * Creates and populates a new #GtkFlowBox with widgets corresponding to
 * every listed @properties. If the list is empty, the created flowbox
 * will be filled by the whole list of properties of the associated
 * #GimpProcedure, in the defined order. This is similar of how
 * gimp_procedure_dialog_fill() works except that it creates a new
 * widget which is not inside @dialog itself.
 *
 * The @container_id must be a unique ID which is neither the name of a
 * property of the #GimpProcedureConfig associated to @dialog, nor is it
 * the ID of any previously created container. This ID can later be used
 * together with property names to be packed in other containers or
 * inside @dialog itself.
 *
 * Returns: (transfer none): the #GtkFlowBox representing @property. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_fill_flowbox_list (GimpProcedureDialog *dialog,
        const gchar         *container_id,
        GList               *properties)
{
    g_return_val_if_fail (container_id != NULL, NULL);

    return gimp_procedure_dialog_fill_container_list (dialog, container_id,
            GTK_CONTAINER (gtk_flow_box_new ()),
            properties);
}


/**
 * gimp_procedure_dialog_fill_frame:
 * @dialog:        the #GimpProcedureDialog.
 * @container_id:  a container identifier.
 * @title_id: (nullable): the identifier for the title widget.
 * @invert_title:  whether to use the opposite value of @title_id if it
 *                 represents a boolean widget.
 * @contents_id: (nullable): the identifier for the contents.
 *
 * Creates a new #GtkFrame and packs @title_id as its title and
 * @contents_id as its child.
 * If @title_id represents a boolean property, its value will be used to
 * renders @contents_id sensitive or not. If @invert_title is TRUE, then
 * sensitivity binding is inverted.
 *
 * The @container_id must be a unique ID which is neither the name of a
 * property of the #GimpProcedureConfig associated to @dialog, nor is it
 * the ID of any previously created container. This ID can later be used
 * together with property names to be packed in other containers or
 * inside @dialog itself.
 *
 * Returns: (transfer none): the #GtkWidget representing @container_id. The
 *                           object belongs to @dialog and must not be
 *                           freed.
 */
GtkWidget *
gimp_procedure_dialog_fill_frame (GimpProcedureDialog *dialog,
                                  const gchar         *container_id,
                                  const gchar         *title_id,
                                  gboolean             invert_title,
                                  const gchar         *contents_id)
{
    GtkWidget *frame;
    GtkWidget *contents = NULL;
    GtkWidget *title    = NULL;

    g_return_val_if_fail (container_id != NULL, NULL);

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->priv->config),
                                      container_id))
    {
        g_warning ("%s: frame identifier '%s' cannot be an existing property name.",
                   G_STRFUNC, container_id);
        return NULL;
    }

    if ((frame = g_hash_table_lookup (dialog->priv->widgets, container_id)))
    {
        g_warning ("%s: frame identifier '%s' was already configured.",
                   G_STRFUNC, container_id);
        return frame;
    }

    frame = gimp_frame_new (NULL);

    if (contents_id)
    {
        contents = gimp_procedure_dialog_get_widget (dialog, contents_id, G_TYPE_NONE);
        if (! contents)
        {
            g_warning ("%s: no property or configured widget with identifier '%s'.",
                       G_STRFUNC, contents_id);
            return frame;
        }

        g_object_ref (contents);
        gtk_container_add (GTK_CONTAINER (frame), contents);
        gtk_widget_show (contents);
    }

    if (title_id)
    {
        title = gimp_procedure_dialog_get_widget (dialog, title_id, G_TYPE_NONE);
        if (! title)
        {
            g_warning ("%s: no property or configured widget with identifier '%s'.",
                       G_STRFUNC, title_id);
            return frame;
        }

        g_object_ref (title);
        gtk_frame_set_label_widget (GTK_FRAME (frame), title);
        gtk_widget_show (title);

        if (contents && (GTK_IS_CHECK_BUTTON (title) || GTK_IS_SWITCH (title)))
        {
            GBindingFlags flags = G_BINDING_SYNC_CREATE;

            if (invert_title)
                flags |= G_BINDING_INVERT_BOOLEAN;

            g_object_bind_property (title,    "active",
                                    contents, "sensitive",
                                    flags);
        }
    }

    g_hash_table_insert (dialog->priv->widgets, g_strdup (container_id), frame);

    return frame;
}

/**
 * gimp_procedure_dialog_run:
 * @dialog: the #GimpProcedureDialog.
 *
 * Show @dialog and only returns when the user finished interacting with
 * it (either validating choices or canceling).
 *
 * Returns: %TRUE if the dialog was validated, %FALSE otherwise.
 */
gboolean
gimp_procedure_dialog_run (GimpProcedureDialog *dialog)
{
    g_return_val_if_fail (GIMP_IS_PROCEDURE_DIALOG (dialog), FALSE);

    while (TRUE)
    {
        gint response = gimp_dialog_run (GIMP_DIALOG (dialog));

        if (response == RESPONSE_RESET)
        {
            if (! dialog->priv->reset_popover)
            {
                GtkWidget *button;
                GtkWidget *vbox;

                button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
                         response);

                dialog->priv->reset_popover = gtk_popover_new (button);

                vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
                gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
                gtk_container_add (GTK_CONTAINER (dialog->priv->reset_popover),
                                   vbox);
                gtk_widget_show (vbox);

                button = gtk_button_new_with_mnemonic (_("Reset to _Initial "
                                                       "Values"));
                gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
                gtk_widget_show (button);

                g_signal_connect (button, "clicked",
                                  G_CALLBACK (gimp_procedure_dialog_reset_initial),
                                  dialog);

                button = gtk_button_new_with_mnemonic (_("Reset to _Factory "
                                                       "Defaults"));
                gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
                gtk_widget_show (button);

                g_signal_connect (button, "clicked",
                                  G_CALLBACK (gimp_procedure_dialog_reset_factory),
                                  dialog);
            }

            gtk_popover_popup (GTK_POPOVER (dialog->priv->reset_popover));
        }
        else
        {
            return response == GTK_RESPONSE_OK;
        }
    }
}


/*  private functions  */

static void
gimp_procedure_dialog_reset_initial (GtkWidget           *button,
                                     GimpProcedureDialog *dialog)
{
    gimp_config_copy (GIMP_CONFIG (dialog->priv->initial_config),
                      GIMP_CONFIG (dialog->priv->config),
                      0);

    gtk_popover_popdown (GTK_POPOVER (dialog->priv->reset_popover));
}

static void
gimp_procedure_dialog_reset_factory (GtkWidget           *button,
                                     GimpProcedureDialog *dialog)
{
    gimp_config_reset (GIMP_CONFIG (dialog->priv->config));

    gtk_popover_popdown (GTK_POPOVER (dialog->priv->reset_popover));
}

static void
gimp_procedure_dialog_load_defaults (GtkWidget           *button,
                                     GimpProcedureDialog *dialog)
{
    GError *error = NULL;

    if (! gimp_procedure_config_load_default (dialog->priv->config, &error))
    {
        if (error)
        {
            g_printerr ("Loading default values from disk failed: %s\n",
                        error->message);
            g_clear_error (&error);
        }
        else
        {
            g_printerr ("No default values found on disk\n");
        }
    }
}

static void
gimp_procedure_dialog_save_defaults (GtkWidget           *button,
                                     GimpProcedureDialog *dialog)
{
    GError *error = NULL;

    if (! gimp_procedure_config_save_default (dialog->priv->config, &error))
    {
        g_printerr ("Saving default values to disk failed: %s\n",
                    error->message);
        g_clear_error (&error);
    }
}

static gboolean
gimp_procedure_dialog_check_mnemonic (GimpProcedureDialog *dialog,
                                      GtkWidget           *widget,
                                      const gchar         *id,
                                      const gchar         *core_id)
{
    GtkWidget *label    = NULL;
    gchar     *duplicate;
    gboolean   success  = TRUE;
    guint      mnemonic = GDK_KEY_VoidSymbol;

    g_return_val_if_fail ((id && ! core_id) || (core_id && ! id), FALSE);

    if (GIMP_IS_LABELED (widget))
    {
        label = gimp_labeled_get_label (GIMP_LABELED (widget));
    }
    else if (g_type_is_a (G_OBJECT_TYPE (widget), GTK_TYPE_LABEL))
    {
        label = widget;
    }
    else if (g_type_is_a (G_OBJECT_TYPE (widget), GTK_TYPE_BUTTON))
    {
        label = gtk_bin_get_child (GTK_BIN (widget));
        if (! label || ! g_type_is_a (G_OBJECT_TYPE (label), GTK_TYPE_LABEL))
            label = NULL;
    }

    if (label                                                          &&
            (mnemonic = gtk_label_get_mnemonic_keyval (GTK_LABEL (label))) &&
            mnemonic != GDK_KEY_VoidSymbol)
    {
        duplicate = g_hash_table_lookup (dialog->priv->core_mnemonics, GINT_TO_POINTER (mnemonic));
        if (duplicate && g_strcmp0 (duplicate, id ? id : core_id) != 0)
        {
            g_printerr ("Procedure '%s': duplicate mnemonic %s for label of property %s and dialog button %s\n",
                        gimp_procedure_get_name (dialog->priv->procedure),
                        gdk_keyval_name (mnemonic), id, duplicate);
            success = FALSE;
        }

        if (success)
        {
            duplicate = g_hash_table_lookup (dialog->priv->mnemonics, GINT_TO_POINTER (mnemonic));
            if (duplicate && g_strcmp0 (duplicate, id ? id : core_id) != 0)
            {
                g_printerr ("Procedure '%s': duplicate mnemonic %s for label of properties %s and %s\n",
                            gimp_procedure_get_name (dialog->priv->procedure),
                            gdk_keyval_name (mnemonic), id, duplicate);
                success = FALSE;
            }
            else if (! duplicate)
            {
                if (id)
                    g_hash_table_insert (dialog->priv->mnemonics, GINT_TO_POINTER (mnemonic), g_strdup (id));
                else
                    g_hash_table_insert (dialog->priv->core_mnemonics, GINT_TO_POINTER (mnemonic), g_strdup (core_id));
            }
        }
    }
    else
    {
        g_printerr ("Procedure '%s': no mnemonic for property %s\n",
                    gimp_procedure_get_name (dialog->priv->procedure), id);
        success = FALSE;
    }

    return success;
}

/**
 * gimp_procedure_dialog_fill_container_list:
 * @dialog:
 * @container_id:
 * @container: (transfer full):
 * @properties:
 *
 * A generic function to be used by various publich functions
 * gimp_procedure_dialog_fill_*_list(). Note in particular that
 * @container is taken over by this function which may return it or not.
 * @container is assumed to be a floating GtkContainer (i.e. newly
 * created widget without a parent yet).
 * If the object returns a different object (because @container_id
 * already represents another widget) or %NULL, the function takes care
 * of freeing @container. Calling code must therefore not reuse the
 * pointer anymore.
 */
static GtkWidget *
gimp_procedure_dialog_fill_container_list (GimpProcedureDialog *dialog,
        const gchar         *container_id,
        GtkContainer        *container,
        GList               *properties)
{
    GList    *iter;
    gboolean  free_properties = FALSE;

    g_return_val_if_fail (container_id != NULL, NULL);
    g_return_val_if_fail (GTK_IS_CONTAINER (container), NULL);
    g_return_val_if_fail (g_object_is_floating (G_OBJECT (container)), NULL);

    g_object_ref_sink (container);
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->priv->config),
                                      container_id))
    {
        g_warning ("%s: container identifier '%s' cannot be an existing property name.",
                   G_STRFUNC, container_id);
        g_object_unref (container);
        return NULL;
    }

    if (g_hash_table_lookup (dialog->priv->widgets, container_id))
    {
        g_warning ("%s: container identifier '%s' was already configured.",
                   G_STRFUNC, container_id);
        g_object_unref (container);
        return g_hash_table_lookup (dialog->priv->widgets, container_id);
    }

    if (! properties)
    {
        GParamSpec **pspecs;
        guint        n_pspecs;
        gint         i;

        pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (dialog->priv->config),
                 &n_pspecs);

        for (i = 0; i < n_pspecs; i++)
        {
            const gchar *prop_name;
            GParamSpec  *pspec = pspecs[i];

            /*  skip our own properties  */
            if (pspec->owner_type == GIMP_TYPE_PROCEDURE_CONFIG)
                continue;

            prop_name  = g_param_spec_get_name (pspec);
            properties = g_list_prepend (properties, (gpointer) prop_name);
        }

        properties = g_list_reverse (properties);

        if (properties)
            free_properties = TRUE;
    }

    for (iter = properties; iter; iter = iter->next)
    {
        GtkWidget *widget;

        widget = gimp_procedure_dialog_get_widget (dialog, iter->data, G_TYPE_NONE);
        if (widget)
        {
            /* Reference the widget because the hash table will
             * unreference it anyway when getting destroyed so we don't
             * want to give the only reference to the parent widget.
             */
            g_object_ref (widget);
            gtk_container_add (container, widget);
            gtk_widget_show (widget);
        }
    }

    if (free_properties)
        g_list_free (properties);

    g_hash_table_insert (dialog->priv->widgets, g_strdup (container_id), container);

    return GTK_WIDGET (container);
}
