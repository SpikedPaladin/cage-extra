#ifndef DBUS_CUSTOM_H
#define DBUS_CUSTOM_H

#include <wayland-server-core.h>
#include "server.h"

void init_dbus(struct cg_server *server, struct wl_event_source **dbus_source);

#endif
