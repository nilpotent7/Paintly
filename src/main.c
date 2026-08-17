#include "app.h"

int main(int argc, char **argv)
{
    App *app = g_new0(App, 1);
    /* NON_UNIQUE: every launch is its own process and its own window.
     * HANDLES_OPEN: file arguments arrive at ::open instead of ::activate. */
    app->gapp = gtk_application_new("org.paintly.Paintly",
                                    G_APPLICATION_NON_UNIQUE |
                                    G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app->gapp, "startup",  G_CALLBACK(app_startup),  app);
    g_signal_connect(app->gapp, "activate", G_CALLBACK(app_activate), app);
    g_signal_connect(app->gapp, "open",     G_CALLBACK(app_open),     app);

    int status = g_application_run(G_APPLICATION(app->gapp), argc, argv);

    g_object_unref(app->gapp);
    if (app->doc)
        document_free(app->doc);
    if (app->layer_thumbs)
        g_ptr_array_unref(app->layer_thumbs);
    g_free(app);
    return status;
}
