#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#ifdef USE_WEBKIT2
#include <webkit2/webkit2.h>
#else
#include <webkit/webkit.h>
#endif
#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gio/gio.h>
#ifdef G_OS_UNIX
#include <gio/gunixfdlist.h>
/* For STDOUT_FILENO */
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyWindowCb(GtkWidget* widget, GtkWidget* window);
static gboolean closeWebViewCb(WebKitWebView* webView, GtkWidget* window);
static void afterMainWindowRealized(GtkWidget* window, gpointer user_data);
/*
static void resourceRequestStarting(WebKitWebView         *web_view,
                                    WebKitWebFrame        *web_frame,
                                    WebKitWebResource     *web_resource,
                                    WebKitNetworkRequest  *request,
                                    WebKitNetworkResponse *response,
                                    gpointer               user_data);
static void requestStartedCb(SoupSession *session,
                             SoupMessage *msg,
                             SoupSocket  *socket,
                             gpointer     user_data);
*/

void serverRequestStartedCb(SoupServer        *server,
                            SoupMessage       *message,
                            SoupClientContext *client,
                            gpointer           user_data);
void serverHandleStatic(SoupServer *server,
                        SoupMessage *msg,
                        const char *path,
                        GHashTable *query,
                        SoupClientContext *client,
                        gpointer user_data);
                                                                                                                 
///////////////////////////////////////////////////////////////////////////////////////////////////

static gchar* bundle_id = NULL;
static gchar* activity_id = NULL;
static gchar* optional_object_id = NULL;
static gchar* optional_uri = NULL;

static GOptionEntry entries[] =
{
    { "bundle-id", 'b', 0, G_OPTION_ARG_STRING, &bundle_id, "Identifier of the activity bundle.", NULL },
    { "activity-id", 'a', 0, G_OPTION_ARG_STRING, &activity_id, "Unique identifier of the activity instance.", NULL },
    { "object-id", 'o', 0, G_OPTION_ARG_STRING, &optional_object_id, "(optional) Identity of the journal object associated with the activity instance.", NULL },
    { "uri", 'u', 0, G_OPTION_ARG_STRING, &optional_uri, "(optional) URI associated with the activity.", NULL },
    { NULL }
};

///////////////////////////////////////////////////////////////////////////////////////////////////

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.laptop.Activity'>"
//    "    <annotation name='org.gtk.GDBus.Annotation' value='OnInterface'/>"
//    "    <annotation name='org.gtk.GDBus.Annotation' value='AlsoOnInterface'/>"
    "    <method name='SetActive'>"
//    "      <annotation name='org.gtk.GDBus.Annotation' value='OnMethod'/>"
    "      <arg type='b' name='active' direction='in'/>"
    "    </method>"
    "    <method name='Invite'>"
//    "      <annotation name='org.gtk.GDBus.Annotation' value='OnMethod'/>"
    "      <arg type='s' name='buddy_key' direction='in'/>"
    "    </method>"
    "    <method name='HandleViewSource'>"
//    "      <annotation name='org.gtk.GDBus.Annotation' value='OnMethod'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data);
static void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data);
static void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer user_data);
/*
static void load_finished_cb(WebKitWebView *web_view, WebKitWebFrame *web_frame, gpointer data) {
      printf("Finished downloading %s\n", webkit_web_view_get_uri(web_view));
}
*/
///////////////////////////////////////////////////////////////////////////////////////////////////
  
int main(int argc, char* argv[])
{
    //ARGH: putting it after g_dbus_node_info_new_for_xml works on Fedora but crashes the XO...
    gtk_init(&argc, &argv);

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new (NULL);
    g_option_context_add_main_entries (context, entries, NULL);
    //g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }
    if(bundle_id == NULL || activity_id == NULL)
    {
        g_print ("bundle-id and activity-id are required options\n");
        exit (1);
    }

/////////////////////////////////////////////////

    introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    g_assert (introspection_data != NULL);

    gchar service_name[256];
    sprintf(service_name, "org.laptop.Activity%s", activity_id);
    
    guint owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION, service_name, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost,
        NULL, NULL);

/////////////////////////////////////////////////
        
    WebKitWebView *webView = WEBKIT_WEB_VIEW(webkit_web_view_new());
/*
    guint n = -1, i;
    guint *alma = g_signal_list_ids(G_TYPE_FROM_INSTANCE(webView), &n);
    for(i=0; i<n; ++i) {
        GSignalQuery q;
        g_signal_query(alma[i], &q);
        printf("alma: %d, %s\n", q.signal_id, q.signal_name);
    }
*/                            
    GtkScrolledWindow *scrolledWindow = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    gtk_scrolled_window_add_with_viewport(scrolledWindow, GTK_WIDGET(webView));
                                                        
    GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_window_maximize(GTK_WINDOW(main_window));
    //ARGH: do not use fullscreen as it disables the frame
    //gtk_window_fullscreen(GTK_WINDOW(main_window));
    gtk_container_add(GTK_CONTAINER(main_window), GTK_WIDGET(scrolledWindow));
    g_signal_connect_after(main_window, "realize", G_CALLBACK(afterMainWindowRealized), NULL);
    g_signal_connect(main_window, "destroy", G_CALLBACK(destroyWindowCb), NULL);

#ifdef USE_WEBKIT2
    g_signal_connect(webView, "close", G_CALLBACK(closeWebViewCb), main_window);
#else
    g_signal_connect(webView, "close-web-view", G_CALLBACK(closeWebViewCb), main_window);
/*
    //g_signal_connect(webView, "resource-request-starting", G_CALLBACK(resourceRequestStarting), main_window);
    SoupSession *session = webkit_get_default_session();
    fprintf(stderr, "session is %s\n", session == NULL ? "null" : "notnull");
    g_signal_connect(session, "request-started", G_CALLBACK(requestStartedCb), main_window);
*/
#endif

    SoupServer *server = soup_server_new(SOUP_SERVER_PORT, 0, NULL); // use some random port
//ARGH: it crashes
//    SoupAddress *address = soup_address_new("localhost", 0);
//    if(address != NULL) {
//        SoupServer *server = soup_server_new(SOUP_SERVER_INTERFACE, address, NULL); // use some random port
//    }
    guint port = soup_server_get_port(server);
    SoupSocket *listener = soup_server_get_listener(server);
    SoupAddress *addr = soup_socket_get_local_address(listener);
    fprintf(stderr, "server host %s port %d\n", soup_address_get_name(addr), port);
    soup_server_add_handler(server, "/activity", serverHandleStatic, NULL, NULL);
    soup_server_add_handler(server, "/web", serverHandleStatic, NULL, NULL);
//    g_signal_connect(server, "request-started", G_CALLBACK(serverRequestStartedCb), NULL);
    soup_server_run_async(server);
    
/*
    gchar *current_dir = g_get_current_dir();
    gchar index_uri[256];
    //ARGH: a file uri always has to be absolute
    sprintf(index_uri, "file://%s/index.html", current_dir);
    webkit_web_view_load_uri(webView, index_uri);
*/
    //webkit_web_view_load_uri(webView, "http://nell-colors.github.cscott.net");
    //webkit_web_view_load_uri(webView, "http://index.hu");

    gchar buffer[256];
    sprintf(buffer, "http://localhost:%d/web/index.html", port);
    webkit_web_view_load_uri(webView, buffer);

    gtk_widget_grab_focus(GTK_WIDGET(webView));
    gtk_widget_show_all(main_window);

    gtk_main();

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyWindowCb(GtkWidget* widget, GtkWidget* window)
{
    gtk_main_quit();
}

static gboolean closeWebViewCb(WebKitWebView* webView, GtkWidget* window)
{
    fprintf(stderr, "closeWebViewCb\n");
    // handle window.close from javascript
    gtk_widget_destroy(window);
    return TRUE;
}

static void setXProperty(GtkWidget* window, const gchar *propertyName, const gchar *propertyValue)
{
    GdkAtom property = gdk_atom_intern_static_string(propertyName);
    gdk_property_change(gtk_widget_get_window(window), property,
        (GdkAtom)XA_STRING, 8, GDK_PROP_MODE_REPLACE,
        (const guchar *)propertyValue, strlen(propertyValue));
}
 
static void afterMainWindowRealized(GtkWidget* window, gpointer user_data)
{
    setXProperty(window, "_SUGAR_ACTIVITY_ID", activity_id);
    setXProperty(window, "_SUGAR_BUNDLE_ID", bundle_id);
    //TODO: set _NET_WM_NAME & _NET_WM_PID
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
    fprintf(stderr, "D-Bus call %s()\n", method_name);
    //TODO: pass methods forward
    g_dbus_method_invocation_return_value (invocation, NULL);
}

static const GDBusInterfaceVTable interface_vtable =
{
    handle_method_call,
    NULL,
    NULL
};

static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    fprintf(stderr, "D-Bus bus acquired: %s\n", name);

    gchar object_path[256];
    sprintf(object_path, "/org/laptop/Activity/%s", activity_id);

    guint registration_id = g_dbus_connection_register_object (
        connection,
        object_path,
        introspection_data->interfaces[0],
        &interface_vtable,
        NULL,  /* user_data */
        NULL,  /* user_data_free_func */
        NULL); /* GError** */
    g_assert (registration_id > 0);
}

static void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    fprintf(stderr, "D-Bus name acquired: %s\n", name);
}

static void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    fprintf(stderr, "D-Bus name lost: %s\n", name);
    exit (1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/*
static void resourceRequestStarting(WebKitWebView         *web_view,
                                    WebKitWebFrame        *web_frame,
                                    WebKitWebResource     *web_resource,
                                    WebKitNetworkRequest  *request,
                                    WebKitNetworkResponse *response,
                                    gpointer               user_data)
{
    fprintf(stderr, "resourceRequestStarting request: %s\n", webkit_network_request_get_uri(request));
    //fprintf(stderr, "resourceRequestStarting response: %s\n", response == NULL ? "NULL" : webkit_network_response_get_uri(response));
    //SoupMessage *message = webkit_network_request_get_message(request);
    //soup_message_set_response(message, "text/html", SOUP_MEMORY_STATIC, "alma", 4);

    SoupSession *session = webkit_get_default_session();
    fprintf(stderr, "session is %s\n", session == NULL ? "null2" : "notnull2");
    g_signal_connect(session, "request-started", G_CALLBACK(requestStartedCb), NULL);
}

static int callcount = 0;

static void requestStartedCb(SoupSession *session,
                             SoupMessage *msg,
                             SoupSocket  *socket,
                             gpointer     user_data)
{
    fprintf(stderr, "requestStartedCb: %s\n", 
        soup_uri_to_string(soup_message_get_uri(msg), false));
    if(callcount < 5) {
        ++callcount;
        soup_message_set_response(msg, "text/html", SOUP_MEMORY_STATIC, "alma", 4);
    } else {
        exit(2);
    }
}                             
*/

///////////////////////////////////////////////////////////////////////////////////////////////////

void serverRequestStartedCb(SoupServer        *server,
                            SoupMessage       *message,
                            SoupClientContext *client,
                            gpointer           user_data)
{
    fprintf(stderr, "serverRequestStartedCb: \n");
}

const gchar *extension_mapping[] = {
    ".html", "text/html",
    ".css", "text/css",
    ".js", "text/javascript",
    ".json", "application/json",
    ".svg", "image/svg+xml",
    NULL
};

static const gchar *getContentType(const gchar *uri) {
    int i;
    for(i=0; extension_mapping[i] != NULL; i += 2) {
        if(g_str_has_suffix(uri, extension_mapping[i]))
            return extension_mapping[i+1];
    }
    return "text";
}

void serverHandleStatic(SoupServer *server,
                        SoupMessage *msg,
                        const char *path,
                        GHashTable *query,
                        SoupClientContext *client,
                        gpointer user_data)
{
    gchar *current_dir = g_get_current_dir();
    gchar full_file_path[256];
    sprintf(full_file_path, "%s%s", current_dir, path);
    bool ok = false;
    //TODO: reject .. in path
    int fd = open(full_file_path, O_RDONLY);
    if(fd != -1) {
        struct stat sstat;
        if(fstat(fd, &sstat) != -1) {
            size_t len = (size_t)sstat.st_size;
            void *addr = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
            if(addr != MAP_FAILED) {
                const gchar *content_type = getContentType(path);
                soup_message_set_response(msg, content_type, SOUP_MEMORY_COPY, addr, len);
                soup_message_set_status(msg, 200);
                fprintf(stderr, "GET: %s, size is %ld, type is '%s'\n", full_file_path, len, content_type);
                ok = true;
            }
        }
        close(fd);
    }
    if(!ok) {
        soup_message_set_status(msg, 404);
        fprintf(stderr, "ERROR: %s\n", full_file_path);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

