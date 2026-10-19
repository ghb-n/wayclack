#include "tray.h"

#include <dbus/dbus.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <unistd.h>

using namespace std;

namespace WayClack {

namespace {

// Menu item ids.
enum MenuId {
  kMenuRoot = 0,
  kMenuMute = 1,
  kMenuVolumeUp = 2,
  kMenuVolumeDown = 3,
  kMenuNextPack = 4,
  kMenuPrevPack = 5,
  kMenuOpenTui = 6,
  kMenuQuit = 7,
};

const char *kItemInterface = "org.kde.StatusNotifierItem";
const char *kItemPath = "/StatusNotifierItem";
const char *kMenuInterface = "com.canonical.dbusmenu";
const char *kMenuPath = "/MenuBar";
const char *kPropsInterface = "org.freedesktop.DBus.Properties";

// Append a basic variant to a message iter.
void appendVariantString(DBusMessageIter *iter, const char *value) {
  DBusMessageIter variant;
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
  dbus_message_iter_close_container(iter, &variant);
}

void appendVariantBool(DBusMessageIter *iter, dbus_bool_t value) {
  DBusMessageIter variant;
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
  dbus_message_iter_close_container(iter, &variant);
}

// One menu entry: id + type + label.
struct MenuEntry {
  int id;
  const char *label;
};

// The static menu layout. Kept in one place so GetLayout and Event agree.
const MenuEntry kMenuEntries[] = {
    {kMenuMute, "Mute"},
    {kMenuVolumeUp, "Volume +"},
    {kMenuVolumeDown, "Volume -"},
    {kMenuNextPack, "Next sound pack"},
    {kMenuPrevPack, "Previous sound pack"},
    {kMenuOpenTui, "Open TUI"},
    {kMenuQuit, "Quit WayClack"},
};

// Build the (ia{sv}av) layout node used by dbusmenu GetLayout / signals.
void appendLayoutNode(DBusMessageIter *iter, int id, const char *label,
                      bool isRoot) {
  DBusMessageIter node;
  dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, nullptr, &node);

  // (i) id
  dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);

  // (a{sv}) properties
  DBusMessageIter props;
  dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);

  // The root node only advertises that it opens a menu; leaf entries get a
  // label and are enabled/visible.
  if (isRoot) {
    DBusMessageIter dict;
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, nullptr,
                                     &dict);
    const char *key = "children-display";
    dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &key);
    appendVariantString(&dict, "submenu");
    dbus_message_iter_close_container(&props, &dict);
  } else {
    // "label" -> variant string
    {
      DBusMessageIter dict;
      dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &dict);
      const char *key = "label";
      dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &key);
      appendVariantString(&dict, label);
      dbus_message_iter_close_container(&props, &dict);
    }
    // "enabled" -> variant bool true
    {
      DBusMessageIter dict;
      dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &dict);
      const char *key = "enabled";
      dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &key);
      appendVariantBool(&dict, TRUE);
      dbus_message_iter_close_container(&props, &dict);
    }
    // "visible" -> variant bool true
    {
      DBusMessageIter dict;
      dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &dict);
      const char *key = "visible";
      dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &key);
      appendVariantBool(&dict, TRUE);
      dbus_message_iter_close_container(&props, &dict);
    }
  }
  dbus_message_iter_close_container(&node, &props);

  // (av) children: an array of variants, each variant wrapping a node struct.
  DBusMessageIter children;
  dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &children);
  if (isRoot) {
    for (const auto &entry : kMenuEntries) {
      // Each child is a variant containing a (ia{sv}av) struct.
      DBusMessageIter childVar;
      dbus_message_iter_open_container(&children, DBUS_TYPE_VARIANT,
                                       "(ia{sv}av)", &childVar);
      appendLayoutNode(&childVar, entry.id, entry.label, false);
      dbus_message_iter_close_container(&children, &childVar);
    }
  }
  dbus_message_iter_close_container(&node, &children);

  dbus_message_iter_close_container(iter, &node);
}

} // namespace

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------
struct TrayIconImpl {
  DBusConnection *conn = nullptr;
  TrayCallbacks callbacks;
  std::string busName;
  std::string iconName;
  std::string title = "WayClack";
  bool registered = false;

  void sendEmptyReply(DBusMessage *msg, DBusConnection *c) {
    if (!msg)
      return;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // Write one StatusNotifierItem property as a variant into `iter`.
  // Returns false for an unknown property name.
  bool writeProperty(DBusMessageIter *iter, const char *prop) {
    if (strcmp(prop, "Category") == 0) {
      appendVariantString(iter, "ApplicationStatus");
      return true;
    }
    if (strcmp(prop, "Id") == 0) {
      appendVariantString(iter, "wayclack");
      return true;
    }
    if (strcmp(prop, "Title") == 0) {
      appendVariantString(iter, title.c_str());
      return true;
    }
    if (strcmp(prop, "Status") == 0) {
      appendVariantString(iter, "Active");
      return true;
    }
    if (strcmp(prop, "IconName") == 0) {
      appendVariantString(iter, iconName.c_str());
      return true;
    }
    if (strcmp(prop, "AttentionIconName") == 0 ||
        strcmp(prop, "OverlayIconName") == 0 ||
        strcmp(prop, "AttentionMovieName") == 0 ||
        strcmp(prop, "IconThemePath") == 0) {
      // Advertised for completeness; we never set these.
      appendVariantString(iter, "");
      return true;
    }
    if (strcmp(prop, "ItemIsMenu") == 0) {
      // false: left-click activates, right-click pops the menu (the usual
      // behaviour panels expect).
      appendVariantBool(iter, FALSE);
      return true;
    }
    if (strcmp(prop, "WindowId") == 0) {
      DBusMessageIter variant;
      dbus_uint32_t v = 0;
      dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "u", &variant);
      dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &v);
      dbus_message_iter_close_container(iter, &variant);
      return true;
    }
    if (strcmp(prop, "Menu") == 0) {
      DBusMessageIter variant;
      const char *v = kMenuPath;
      dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "o", &variant);
      dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &v);
      dbus_message_iter_close_container(iter, &variant);
      return true;
    }
    if (strcmp(prop, "IconPixmap") == 0 || strcmp(prop, "AttentionIconPixmap") == 0 ||
        strcmp(prop, "OverlayIconPixmap") == 0) {
      DBusMessageIter variant, arr;
      dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a(iiay)",
                                       &variant);
      dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(iiay)",
                                       &arr);
      dbus_message_iter_close_container(&variant, &arr);
      dbus_message_iter_close_container(iter, &variant);
      return true;
    }
    if (strcmp(prop, "ToolTip") == 0) {
      // (sa(iiay)ss): icon name, pixmaps, title, subtitle. Panels read the
      // title for the hover tooltip.
      DBusMessageIter variant, st, arr;
      dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(sa(iiay)ss)",
                                       &variant);
      dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr,
                                       &st);
      const char *icon = iconName.c_str();
      dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &icon);
      dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &arr);
      dbus_message_iter_close_container(&st, &arr);
      const char *t = title.c_str();
      const char *sub = "";
      dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &t);
      dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &sub);
      dbus_message_iter_close_container(&variant, &st);
      dbus_message_iter_close_container(iter, &variant);
      return true;
    }
    return false;
  }

  // org.freedesktop.DBus.Properties.Get(interface, name) -------------------
  void handlePropertiesGet(DBusConnection *c, DBusMessage *msg) {
    const char *iface = nullptr;
    const char *prop = nullptr;
    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
      return;

    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    if (!writeProperty(&iter, prop)) {
      // Unknown property: an empty string variant keeps the host happy.
      appendVariantString(&iter, "");
    }

    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  void handlePropertiesGetAll(DBusConnection *c, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, dict;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

    // Return the full standard set; some panel implementations validate the
    // property list and skip items that omit required entries.
    static const char *kProps[] = {
        "Category",       "Id",
        "Title",          "Status",
        "IconName",       "IconPixmap",
        "AttentionIconName", "AttentionIconPixmap",
        "OverlayIconName", "OverlayIconPixmap",
        "AttentionMovieName", "ItemIsMenu",
        "Menu",           "ToolTip",
        "WindowId",       "IconThemePath"};
    for (const char *prop : kProps) {
      DBusMessageIter entry;
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &entry);
      dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &prop);
      if (!writeProperty(&entry, prop))
        appendVariantString(&entry, "");
      dbus_message_iter_close_container(&dict, &entry);
    }

    dbus_message_iter_close_container(&iter, &dict);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // com.canonical.dbusmenu.GetLayout(parentId, depth, names) --------------
  void handleMenuGetLayout(DBusConnection *c, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);

    // (u) revision
    dbus_uint32_t revision = 1;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &revision);

    // (a(ia{sv}av)): an array holding the single root node, whose children are
    // our menu entries.
    DBusMessageIter layout;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv}av)",
                                     &layout);
    appendLayoutNode(&layout, kMenuRoot, "", true);
    dbus_message_iter_close_container(&iter, &layout);

    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // com.canonical.dbusmenu.GetGroupProperties(ids, names) -----------------
  void handleMenuGetGroupProperties(DBusConnection *c, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
    // We can return an empty list; hosts fall back to GetLayout.
    dbus_message_iter_close_container(&iter, &arr);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // com.canonical.dbusmenu.GetProperty(id, name) -> v ---------------------
  void handleMenuGetProperty(DBusConnection *c, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    // Best-effort: return an empty string variant for any requested property.
    appendVariantString(&iter, "");
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // org.freedesktop.DBus.Properties.Get on com.canonical.dbusmenu --------
  void handleMenuPropertiesGet(DBusConnection *c, DBusMessage *msg,
                               bool getAll) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, dict;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

    auto putU = [&](const char *name, dbus_uint32_t value) {
      DBusMessageIter entry, variant;
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &entry);
      dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
      dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);
      dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);
      dbus_message_iter_close_container(&entry, &variant);
      dbus_message_iter_close_container(&dict, &entry);
    };
    auto putS = [&](const char *name, const char *value) {
      DBusMessageIter entry;
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                       &entry);
      dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
      appendVariantString(&entry, value);
      dbus_message_iter_close_container(&dict, &entry);
    };

    if (getAll) {
      putU("Version", 3);
      putS("TextDirection", "ltr");
      putS("Status", "normal");
    } else {
      putU("Version", 3);
    }

    dbus_message_iter_close_container(&iter, &dict);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // com.canonical.dbusmenu.Event(id, eventId, data, timestamp) ------------
  void handleMenuEvent(DBusConnection *c, DBusMessage *msg) {
    dbus_int32_t id = 0;
    const char *event = nullptr;
    DBusMessageIter it;
    if (dbus_message_iter_init(msg, &it)) {
      dbus_message_iter_get_basic(&it, &id);
      dbus_message_iter_next(&it);
      if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&it, &event);
    }

    sendEmptyReply(msg, c); // Ack first so the host stays responsive.

    if (event == nullptr || strcmp(event, "clicked") != 0)
      return;

    switch (id) {
    case kMenuMute:
      if (callbacks.onMuteToggle)
        callbacks.onMuteToggle();
      break;
    case kMenuVolumeUp:
      if (callbacks.onVolumeUp)
        callbacks.onVolumeUp();
      break;
    case kMenuVolumeDown:
      if (callbacks.onVolumeDown)
        callbacks.onVolumeDown();
      break;
    case kMenuNextPack:
      if (callbacks.onNextPack)
        callbacks.onNextPack();
      break;
    case kMenuPrevPack:
      if (callbacks.onPrevPack)
        callbacks.onPrevPack();
      break;
    case kMenuOpenTui:
      if (callbacks.onOpenTui)
        callbacks.onOpenTui();
      break;
    case kMenuQuit:
      if (callbacks.onQuit)
        callbacks.onQuit();
      break;
    default:
      break;
    }
  }

  // Overview / Activate / SecondaryActivate stub handlers -----------------
  void handleNoop(DBusConnection *c, DBusMessage *msg) {
    sendEmptyReply(msg, c);
  }

  // org.freedesktop.DBus.Introspectable.Introspect -------------------------
  //
  // Panels (Plasma included) call Introspect before using an object. If the
  // reply is missing or malformed the host may give up on the item - which
  // shows up as a tray icon with no menu. So we return real XML.
  void handleIntrospect(DBusConnection *c, DBusMessage *msg) {
    const char *path = dbus_message_get_path(msg);
    string xml;
    if (path != nullptr && strcmp(path, kMenuPath) == 0) {
      xml =
          "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
          "Introspection 1.0//EN\"\n"
          "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
          "<node>\n"
          "  <interface name=\"com.canonical.dbusmenu\">\n"
          "    <method name=\"GetLayout\">\n"
          "      <arg type=\"i\" name=\"parentId\" direction=\"in\"/>\n"
          "      <arg type=\"i\" name=\"recursionDepth\" direction=\"in\"/>\n"
          "      <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"
          "      <arg type=\"u\" name=\"revision\" direction=\"out\"/>\n"
          "      <arg type=\"(ia{sv}av)\" name=\"layout\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <method name=\"GetGroupProperties\">\n"
          "      <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"
          "      <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"
          "      <arg type=\"a(ia{sv})\" name=\"properties\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <method name=\"GetProperty\">\n"
          "      <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"
          "      <arg type=\"s\" name=\"name\" direction=\"in\"/>\n"
          "      <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <method name=\"Event\">\n"
          "      <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"
          "      <arg type=\"s\" name=\"eventId\" direction=\"in\"/>\n"
          "      <arg type=\"v\" name=\"data\" direction=\"in\"/>\n"
          "      <arg type=\"u\" name=\"timestamp\" direction=\"in\"/>\n"
          "    </method>\n"
          "    <method name=\"AboutToShow\">\n"
          "      <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"
          "      <arg type=\"b\" name=\"needUpdate\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <method name=\"AboutToShowGroup\">\n"
          "      <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"
          "      <arg type=\"ai\" name=\"updatesNeeded\" direction=\"out\"/>\n"
          "      <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <signal name=\"ItemsPropertiesUpdated\">\n"
          "      <arg type=\"a(ia{sv})\" name=\"updatedProps\"/>\n"
          "      <arg type=\"a(ias)\" name=\"removedProps\"/>\n"
          "    </signal>\n"
          "    <signal name=\"LayoutUpdated\">\n"
          "      <arg type=\"u\" name=\"revision\"/>\n"
          "      <arg type=\"i\" name=\"parent\"/>\n"
          "    </signal>\n"
          "    <property name=\"Version\" type=\"u\" access=\"read\"/>\n"
          "    <property name=\"TextDirection\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"IconThemePath\" type=\"as\" access=\"read\"/>\n"
          "  </interface>\n"
          "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
          "    <method name=\"Introspect\">\n"
          "      <arg type=\"s\" name=\"xml_data\" direction=\"out\"/>\n"
          "    </method>\n"
          "  </interface>\n"
          "</node>\n";
    } else {
      xml =
          "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
          "Introspection 1.0//EN\"\n"
          "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
          "<node>\n"
          "  <interface name=\"org.kde.StatusNotifierItem\">\n"
          "    <method name=\"ContextMenu\">\n"
          "      <arg type=\"i\" name=\"x\" direction=\"in\"/>\n"
          "      <arg type=\"i\" name=\"y\" direction=\"in\"/>\n"
          "    </method>\n"
          "    <method name=\"Activate\">\n"
          "      <arg type=\"i\" name=\"x\" direction=\"in\"/>\n"
          "      <arg type=\"i\" name=\"y\" direction=\"in\"/>\n"
          "    </method>\n"
          "    <method name=\"SecondaryActivate\">\n"
          "      <arg type=\"i\" name=\"x\" direction=\"in\"/>\n"
          "      <arg type=\"i\" name=\"y\" direction=\"in\"/>\n"
          "    </method>\n"
          "    <method name=\"Scroll\">\n"
          "      <arg type=\"i\" name=\"delta\" direction=\"in\"/>\n"
          "      <arg type=\"s\" name=\"orientation\" direction=\"in\"/>\n"
          "    </method>\n"
          "    <signal name=\"NewIcon\"/>\n"
          "    <signal name=\"NewToolTip\"/>\n"
          "    <signal name=\"NewStatus\">\n"
          "      <arg type=\"s\" name=\"status\"/>\n"
          "    </signal>\n"
          "    <property name=\"Category\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"Id\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"Title\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"
          "    <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
          "    <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"
          "    <property name=\"Menu\" type=\"o\" access=\"read\"/>\n"
          "    <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>\n"
          "  </interface>\n"
          "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
          "    <method name=\"Introspect\">\n"
          "      <arg type=\"s\" name=\"xml_data\" direction=\"out\"/>\n"
          "    </method>\n"
          "  </interface>\n"
          "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
          "    <method name=\"Get\">\n"
          "      <arg type=\"s\" name=\"interface_name\" direction=\"in\"/>\n"
          "      <arg type=\"s\" name=\"property_name\" direction=\"in\"/>\n"
          "      <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <method name=\"GetAll\">\n"
          "      <arg type=\"s\" name=\"interface_name\" direction=\"in\"/>\n"
          "      <arg type=\"a{sv}\" name=\"properties\" direction=\"out\"/>\n"
          "    </method>\n"
          "    <signal name=\"PropertiesChanged\">\n"
          "      <arg type=\"s\" name=\"interface_name\"/>\n"
          "      <arg type=\"a{sv}\" name=\"changed_properties\"/>\n"
          "      <arg type=\"as\" name=\"invalidated_properties\"/>\n"
          "    </signal>\n"
          "  </interface>\n"
          "</node>\n";
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    const char *str = xml.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &str);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // com.canonical.dbusmenu.AboutToShow(id) -> b --------------------------
  void handleMenuAboutToShow(DBusConnection *c, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    dbus_bool_t needUpdate = FALSE;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &needUpdate);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // com.canonical.dbusmenu.AboutToShowGroup(ids) -> (ai, ai) -------------
  void handleMenuAboutToShowGroup(DBusConnection *c, DBusMessage *msg) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr1, arr2;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &arr1);
    dbus_message_iter_close_container(&iter, &arr1);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &arr2);
    dbus_message_iter_close_container(&iter, &arr2);
    dbus_connection_send(c, reply, nullptr);
    dbus_message_unref(reply);
  }

  // Dispatch a single incoming message.
  void dispatch(DBusMessage *msg) {
    const char *iface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path = dbus_message_get_path(msg);
    if (iface == nullptr || member == nullptr)
      return;

    const bool onMenu = (path != nullptr && strcmp(path, kMenuPath) == 0);

    if (strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0) {
      if (strcmp(member, "Introspect") == 0)
        handleIntrospect(conn, msg);
      else
        handleNoop(conn, msg);
      return;
    }
    if (strcmp(iface, kPropsInterface) == 0) {
      if (strcmp(member, "Get") == 0) {
        if (onMenu)
          handleMenuPropertiesGet(conn, msg, false);
        else
          handlePropertiesGet(conn, msg);
      } else if (strcmp(member, "GetAll") == 0) {
        if (onMenu)
          handleMenuPropertiesGet(conn, msg, true);
        else
          handlePropertiesGetAll(conn, msg);
      } else {
        handleNoop(conn, msg);
      }
      return;
    }
    if (strcmp(iface, kMenuInterface) == 0) {
      if (strcmp(member, "GetLayout") == 0)
        handleMenuGetLayout(conn, msg);
      else if (strcmp(member, "GetGroupProperties") == 0)
        handleMenuGetGroupProperties(conn, msg);
      else if (strcmp(member, "GetProperty") == 0)
        handleMenuGetProperty(conn, msg);
      else if (strcmp(member, "Event") == 0)
        handleMenuEvent(conn, msg);
      else if (strcmp(member, "AboutToShow") == 0)
        handleMenuAboutToShow(conn, msg);
      else if (strcmp(member, "AboutToShowGroup") == 0)
        handleMenuAboutToShowGroup(conn, msg);
      else
        handleNoop(conn, msg);
      return;
    }
    if (strcmp(iface, kItemInterface) == 0) {
      if (strcmp(member, "Activate") == 0 ||
          strcmp(member, "SecondaryActivate") == 0) {
        // Left-click on the icon opens the TUI (ItemIsMenu is false, so the
        // panel sends Activate and pops the menu itself on right-click).
        sendEmptyReply(msg, conn);
        if (callbacks.onOpenTui)
          callbacks.onOpenTui();
        return;
      }
      // ContextMenu / Scroll: the host pops our dbusmenu itself.
      handleNoop(conn, msg);
      return;
    }
  }

  // Register with the StatusNotifierWatcher.
  bool registerItem() {
    DBusMessage *msg = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
    const char *name = busName.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        conn, msg, 2000, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err)) {
      dbus_error_free(&err);
      return false;
    }
    if (reply)
      dbus_message_unref(reply);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
TrayIcon::TrayIcon() : impl_(new TrayIconImpl()) {}

TrayIcon::~TrayIcon() {
  stop();
  delete impl_;
  impl_ = nullptr;
}

// Vtable callback: invoked by libdbus for each method call on a registered
// object path. Routes into Impl::dispatch.
static DBusHandlerResult trayMessageHandler(DBusConnection *conn,
                                            DBusMessage *msg, void *userData) {
  auto *impl = static_cast<TrayIconImpl *>(userData);
  if (impl == nullptr)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  // Only handle method calls; let signals pass through.
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  impl->conn = conn;
  impl->dispatch(msg);
  return DBUS_HANDLER_RESULT_HANDLED;
}

bool TrayIcon::start(const string &iconName, const TrayCallbacks &callbacks) {
  DBusError err;
  dbus_error_init(&err);
  impl_->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (impl_->conn == nullptr) {
    if (dbus_error_is_set(&err))
      dbus_error_free(&err);
    return false;
  }
  // Never exit on disconnect; we manage teardown ourselves.
  dbus_connection_set_exit_on_disconnect(impl_->conn, FALSE);

  impl_->callbacks = callbacks;
  impl_->iconName = iconName;

  // Register the two object paths the SNI + dbusmenu specs require.
  static DBusObjectPathVTable vtable = {
      nullptr, trayMessageHandler, nullptr, nullptr, nullptr, nullptr};

  if (!dbus_connection_register_object_path(impl_->conn, kItemPath, &vtable,
                                            impl_) ||
      !dbus_connection_register_object_path(impl_->conn, kMenuPath, &vtable,
                                            impl_)) {
    return false;
  }

  // Request a well-known per-process bus name.
  string requested = "org.kde.StatusNotifierItem-" + to_string(getpid()) + "-1";
  int ret = dbus_bus_request_name(impl_->conn, requested.c_str(),
                                  DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (dbus_error_is_set(&err)) {
    dbus_error_free(&err);
    return false;
  }
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    return false;

  impl_->busName = requested;
  impl_->registered = impl_->registerItem();

  running_.store(true);
  return true;
}

void TrayIcon::run() {
  if (impl_ == nullptr || impl_->conn == nullptr)
    return;

  while (running_.load()) {
    // Block up to 200ms so stop() is checked regularly without busy-waiting.
    if (!dbus_connection_read_write_dispatch(impl_->conn, 200))
      break; // Connection closed.
  }
}

void TrayIcon::stop() {
  if (!running_.exchange(false))
    return;

  if (impl_ != nullptr && impl_->conn != nullptr) {
    // Release the bus name; the watcher notices the item is gone.
    DBusError err;
    dbus_error_init(&err);
    if (!impl_->busName.empty())
      dbus_bus_release_name(impl_->conn, impl_->busName.c_str(), &err);
    if (dbus_error_is_set(&err))
      dbus_error_free(&err);

    dbus_connection_flush(impl_->conn);
    dbus_connection_unref(impl_->conn);
    impl_->conn = nullptr;
  }
}

void TrayIcon::setTitle(const string &title) {
  if (impl_ == nullptr)
    return;
  impl_->title = title;
  // Best-effort: announce the property change.
  if (impl_->conn == nullptr)
    return;
  DBusMessage *sig = dbus_message_new_signal(
      kItemPath, kPropsInterface, "PropertiesChanged");
  DBusMessageIter iter, dict;
  dbus_message_iter_init_append(sig, &iter);
  const char *iface = kItemInterface;
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
  {
    DBusMessageIter entry;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr,
                                     &entry);
    const char *key = "Title";
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    appendVariantString(&entry, title.c_str());
    dbus_message_iter_close_container(&dict, &entry);
  }
  dbus_message_iter_close_container(&iter, &dict);
  dbus_connection_send(impl_->conn, sig, nullptr);
  dbus_message_unref(sig);
}

} // namespace WayClack
