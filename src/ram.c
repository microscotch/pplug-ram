/*============================================================================
Copyright (c) 2018-2025 Raspberry Pi
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
============================================================================*/

#include <locale.h>
#include <glib/gi18n.h>

#ifdef LXPLUG
#include "plugin.h"
#else
#include "lxutils.h"
#endif

#include "ram.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

conf_table_t conf_table[4] = {
    {CONF_TYPE_BOOL,     "show_percentage",     N_("Show usage as percentage"),             NULL},
    {CONF_TYPE_COLOUR,   "foreground",          N_("Foreground colour"),                    NULL},
    {CONF_TYPE_COLOUR,   "background",          N_("Background colour"),                    NULL},
    {CONF_TYPE_NONE,     NULL,                  NULL,                                       NULL}
};

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static gboolean ram_update (RAMPlugin *c);

/*----------------------------------------------------------------------------*/
/* Function definitions                                                       */
/*----------------------------------------------------------------------------*/

/* Periodic timer callback */

static gboolean ram_update (RAMPlugin *c)
{
    FILE *meminfo;
    char buffer[80];
    long int mem_total = 0;
    long int mem_available  = 0;
    unsigned int readmask = 0x2 | 0x1;

    if (g_source_is_destroyed (g_main_current_source ())) return FALSE;

    /* Open statistics file and scan out CPU usage */
    meminfo = fopen ("/proc/meminfo", "r");
    if (meminfo == NULL) return TRUE;
    while (readmask && fgets(buffer, sizeof(buffer), meminfo)) {
        if (sscanf(buffer, "MemTotal: %ld kB\n", &mem_total) == 1) {
            readmask ^= 0x1;
            continue;
        }
        if (sscanf(buffer, "MemAvailable: %ld kB\n", &mem_available) == 1) {
            readmask ^= 0x2;
            continue;
        }
    }

    fclose (meminfo);
    
    if (readmask) {
        g_warning("monitors: Couldn't read all values from /proc/meminfo: "
                  "readmask %x", readmask);
        return FALSE;
    }

    c->total = mem_total;

    /* Adding stats to the buffer:
     * It is debatable if 'mem_buffers' counts as free or not. I'll go with
     * 'free', because it can be flushed fairly quickly, and generally
     * isn't necessary to keep in memory.
     * It is hard to draw the line, which caches should be counted as free,
     * and which not. 'free' command line utility from procps counts
     * SReclaimable as free so it's counted it here as well (note that
     * 'man free' doesn't specify this)
     * 'mem_cached' definitely counts as 'free' because it is immediately
     * released should any application need it. */
    float mem_used = mem_total - mem_available;
    mem_used /= (float)mem_total;
    if (c->show_percentage) sprintf (buffer, "M:%3.0f%%", mem_used * 100.0);
    else buffer[0] = 0;

    graph_new_point (&(c->graph), mem_used, 0, buffer);

    return TRUE;
}

/*----------------------------------------------------------------------------*/
/* wf-panel plugin functions                                                  */
/*----------------------------------------------------------------------------*/

/* Handler for system config changed message from panel */
void ram_update_display (RAMPlugin *c)
{
    GdkRGBA none = {0, 0, 0, 0};
    graph_reload (&(c->graph), wrap_icon_size (c), c->background_colour, c->foreground_colour, none, none);
}

void ram_init (RAMPlugin *c)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    /* Allocate icon as a child of top level */
    graph_init (&(c->graph));
    gtk_container_add (GTK_CONTAINER (c->plugin), c->graph.da);

    ram_update_display (c);

    /* Connect a timer to refresh the statistics. */
    c->timer = g_timeout_add (1500, (GSourceFunc) ram_update, (gpointer) c);

    /* Show the widget and return. */
    gtk_widget_show_all (c->plugin);
}

void ram_destructor (gpointer user_data)
{
    RAMPlugin *c = (RAMPlugin *) user_data;
    graph_free (&(c->graph));
    if (c->timer) g_source_remove (c->timer);
    g_free (c);
}

/*----------------------------------------------------------------------------*/
/* LXPanel plugin functions                                                   */
/*----------------------------------------------------------------------------*/
#ifdef LXPLUG

/* Constructor */
static GtkWidget *ram_constructor (LXPanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context */
    RAMPlugin *c = g_new0 (RAMPlugin, 1);

    /* Allocate top level widget and set into plugin widget pointer. */
    c->panel = panel;
    c->settings = settings;
    c->plugin = gtk_event_box_new ();
    lxpanel_plugin_set_data (c->plugin, c, ram_destructor);

    /* Set config defaults */
    gdk_rgba_parse (&c->foreground_colour, "dark gray");
    gdk_rgba_parse (&c->background_colour, "light gray");
    c->show_percentage = TRUE;

    /* Read config */
    conf_table[0].value = (void *) &c->show_percentage;
    conf_table[1].value = (void *) &c->foreground_colour;
    conf_table[2].value = (void *) &c->background_colour;
    lxplug_read_settings (c->settings, conf_table);

    ram_init (c);

    return c->plugin;
}

/* Handler for system config changed message from panel */
static void ram_configuration_changed (LXPanel *, GtkWidget *plugin)
{
    RAMPlugin *c = lxpanel_plugin_get_data (plugin);
    ram_update_display (c);
}

/* Apply changes from config dialog */
static gboolean ram_apply_configuration (gpointer user_data)
{
    RAMPlugin *c = lxpanel_plugin_get_data (GTK_WIDGET (user_data));

    lxplug_write_settings (c->settings, conf_table);

    ram_update_display (c);
    return FALSE;
}

/* Display configuration dialog */
static GtkWidget *ram_configure (LXPanel *panel, GtkWidget *plugin)
{
    return lxpanel_generic_config_dlg_new (_(PLUGIN_TITLE), panel,
        ram_apply_configuration, plugin,
        conf_table);
}

int module_lxpanel_gtk_version = 1;
char module_name[] = PLUGIN_NAME;

/* Plugin descriptor */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = PLUGIN_TITLE,
    .config = ram_configure,
    .description = N_("Display RAM usage"),
    .new_instance = ram_constructor,
    .reconfigure = ram_configuration_changed,
    .gettext_package = GETTEXT_PACKAGE
};
#endif

/* End of file */
/*----------------------------------------------------------------------------*/
