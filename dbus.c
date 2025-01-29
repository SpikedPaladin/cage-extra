#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <dbus/dbus.h>
#include <string.h>
#include "server.h"
#include "output.h"
#include "view.h"

const char *server_introspection_xml =
	DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE "<node>\n"

						  "  <interface name='org.freedesktop.DBus.Introspectable'>\n"
						  "    <method name='Introspect'>\n"
						  "      <arg name='data' type='s' direction='out' />\n"
						  "    </method>\n"
						  "  </interface>\n"

						  "  <interface name='org.freedesktop.DBus.Properties'>\n"
						  "    <method name='Get'>\n"
						  "      <arg name='interface' type='s' direction='in' />\n"
						  "      <arg name='property'  type='s' direction='in' />\n"
						  "      <arg name='value'     type='s' direction='out' />\n"
						  "    </method>\n"
						  "    <method name='GetAll'>\n"
						  "      <arg name='interface'  type='s'     direction='in'/>\n"
						  "      <arg name='properties' type='a{sv}' direction='out'/>\n"
						  "    </method>\n"
						  "  </interface>\n"

						  "  <interface name='me.paladin.Cage'>\n"
						  "    <property name='Version' type='s' access='read' />\n"
						  "    <method name='Windows' >\n"
						  "      <arg type='i' direction='out' />\n"
						  "    </method>\n"
						  "    <method name='Close'>\n"
						  "    </method>\n"
						  "    <method name='Flip'/>\n"
						  "    <method name='EnableSplit'/>\n"
						  "    <method name='AndroidSplit'/>\n"
						  "    <method name='DisableSplit'/>\n"
						  "    <method name='SideLeft'/>\n"
						  "    <method name='SideRight'/>\n"
						  "    <method name='Quit'>\n"
						  "    </method>\n"
						  "  </interface>\n"

						  "</node>\n";

/*
 * This implements 'Get' method of DBUS_INTERFACE_PROPERTIES so a
 * client can inspect the properties/attributes of 'TestInterface'.
 */
DBusHandlerResult
server_get_properties_handler(const char *property, DBusConnection *conn, DBusMessage *reply)
{
	if (!strcmp(property, "Version")) {
		dbus_message_append_args(reply, DBUS_TYPE_STRING, "0.1", DBUS_TYPE_INVALID);
	} else
		/* Unknown property */
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!dbus_connection_send(conn, reply, NULL))
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * This implements 'GetAll' method of DBUS_INTERFACE_PROPERTIES. This
 * one seems required by g_dbus_proxy_get_cached_property().
 */
DBusHandlerResult
server_get_all_properties_handler(DBusConnection *conn, DBusMessage *reply)
{
	DBusHandlerResult result;
	DBusMessageIter array, dict, iter, variant;
	const char *property = "Version";

	/*
	 * All dbus functions used below might fail due to out of
	 * memory error. If one of them fails, we assume that all
	 * following functions will fail too, including
	 * dbus_connection_send().
	 */
	result = DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);

	/* Append all properties name/value pairs */
	property = "Version";
	dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict);
	dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &property);
	dbus_message_iter_open_container(&dict, DBUS_TYPE_VARIANT, "s", &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, "0.1");
	dbus_message_iter_close_container(&dict, &variant);
	dbus_message_iter_close_container(&array, &dict);

	dbus_message_iter_close_container(&iter, &array);

	if (dbus_connection_send(conn, reply, NULL))
		result = DBUS_HANDLER_RESULT_HANDLED;
	return result;
}

DBusHandlerResult
server_message_handler(DBusConnection *conn, DBusMessage *message, void *data)
{
	struct cg_server *server = data;
	DBusHandlerResult result;
	DBusMessage *reply = NULL;
	DBusError err;

	fprintf(stderr, "Got D-Bus request: %s.%s on %s\n", dbus_message_get_interface(message),
		dbus_message_get_member(message), dbus_message_get_path(message));

	/*
	 * Does not allocate any memory; the error only needs to be
	 * freed if it is set at some point.
	 */
	dbus_error_init(&err);

	if (dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE, "Introspect")) {
		if (!(reply = dbus_message_new_method_return(message)))
			goto fail;

		dbus_message_append_args(reply, DBUS_TYPE_STRING, &server_introspection_xml, DBUS_TYPE_INVALID);
	} else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Get")) {
		const char *interface, *property;

		if (!dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property,
					   DBUS_TYPE_INVALID))
			goto fail;

		if (!(reply = dbus_message_new_method_return(message)))
			goto fail;

		result = server_get_properties_handler(property, conn, reply);
		dbus_message_unref(reply);
		return result;
	} else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "GetAll")) {
		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "Windows")) {
		int count = wl_list_length(&server->views);

		if (!(reply = dbus_message_new_method_return(message)))
			goto fail;

		dbus_message_append_args (reply, DBUS_TYPE_INT32, &count, DBUS_TYPE_INVALID);
	}else if (dbus_message_is_method_call(message, "me.paladin.Cage", "Close")) {
		struct cg_view *view;
		wl_list_for_each_reverse (view, &server->views, link) {
			if (strcmp(view_get_title(view), "Kiosk") == 0 || strcmp(view_get_title(view), "KioskOverlay") == 0)
				continue;

			view_close(view);
			break;
		}
		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "Flip")) {

		struct cg_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			struct wlr_output_state *state = calloc(1, sizeof(struct wlr_output_state));
			wlr_output_state_init(state);

			if (output->wlr_output->transform == WL_OUTPUT_TRANSFORM_NORMAL) {
				server->flipped = true;
				wlr_output_state_set_transform(state, WL_OUTPUT_TRANSFORM_180);
			} else {
				server->flipped = false;
				wlr_output_state_set_transform(state, WL_OUTPUT_TRANSFORM_NORMAL);
			}

			wlr_output_commit_state(output->wlr_output, state);
			wlr_output_state_finish(state);

			break;
		}
		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "Overlay")) {
		raise_view(server, "KioskOverlay");

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "Raise")) {
		raise_view(server, "Kiosk");

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "EnableSplit")) {
		server->mode = OTHER;

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "AndroidSplit")) {
		server->mode = ANDROID;

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "DisableSplit")) {
		server->mode = NONE;

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "SideLeft")) {
		server->side = LEFT;

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "SideRight")) {
		server->side = RIGHT;

		reply = dbus_message_new_method_return(message);
	} else if (dbus_message_is_method_call(message, "me.paladin.Cage", "Quit")) {
		reply = dbus_message_new_method_return(message);

		server_terminate(server);
	} else
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

fail:
	if (dbus_error_is_set(&err)) {
		if (reply)
			dbus_message_unref(reply);
		reply = dbus_message_new_error(message, err.name, err.message);
		dbus_error_free(&err);
	}

	/*
	 * In any cases we should have allocated a reply otherwise it
	 * means that we failed to allocate one.
	 */
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	/* Send the reply which might be an error one too. */
	result = DBUS_HANDLER_RESULT_HANDLED;
	if (!dbus_connection_send(conn, reply, NULL))
		result = DBUS_HANDLER_RESULT_NEED_MEMORY;
	dbus_message_unref(reply);

	return result;
}

const DBusObjectPathVTable server_vtable = {.message_function = server_message_handler};

static int
read_write(int fd, uint32_t mask, void *data)
{
	dbus_connection_read_write_dispatch(data, 0);

	return 0;
}

void
init_dbus(struct cg_server *server, struct wl_event_source **dbus_source)
{
	DBusConnection *conn;
	DBusError err;
	int rv;

        dbus_error_init(&err);

	/* connect to the daemon bus */
	conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!conn) {
		fprintf(stderr, "Failed to get a session DBus connection: %s\n", err.message);
		goto fail;
	}

	rv = dbus_bus_request_name(conn, "me.paladin.Cage", DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
	if (rv != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		fprintf(stderr, "Failed to request name on bus: %s\n", err.message);
		goto fail;
	}

	if (!dbus_connection_register_object_path(conn, "/me/paladin/Cage", &server_vtable, server)) {
		wlr_log(WLR_ERROR, "Failed to register a object path for 'Cage'");
		goto fail;
	}

	server->conn = conn;

	int fd;
	dbus_connection_get_unix_fd(conn, &fd);

	struct wl_event_loop *event_loop = wl_display_get_event_loop(server->wl_display);

	wl_event_loop_add_fd(event_loop, fd, WL_EVENT_READABLE | WL_EVENT_WRITABLE, read_write, conn);
fail:
	dbus_error_free(&err);
	return;
}
