# Use the system tray

The System Tray gives the user access to applications that register a status
item, including applications with no open window. It is separate from the
Notification Log and the Node app list.

1. The launcher starts a private application bus for the session. desktopd
   registers the StatusNotifier watcher and host on that bus. It does not
   take ownership on the parent desktop's bus.
2. An application registers an item. desktopd reads its properties and
   publishes the tray state. Item signals and bus-name loss update that
   state; desktopd does not scan processes.
3. Panel shows an accessible System Tray button directly beside the
   bell on Home and in Nodes only while at least one item is present. Clicking opens a compact
   analog glass tray above the button. Escape, an outside click, or changing between Home and Nodes dismisses
   it. Switching to a different Node also dismisses it. No global shortcut is added.
4. Each item shows one centered icon with equal padding and no visible
   app name. The app name remains available to assistive technology.
   Click an icon to invoke the app's default action. Right click (or press the
   context-menu key while focused) to see the app's DBusMenu. A menu-only item
   opens its menu on either click. Choose an enabled menu entry to send that
   choice to the application. If no DBusMenu is supplied, ask the application
   to show its own menu.
5. If an application's registration disappears, remove its icon and close
   any open menu for it. When the last item is gone, close the tray and hide
   the button. A delayed reply to a dismissed menu does not reopen it.

The tray is session-wide and is not scoped to a Node. It does not index
unregistered apps, applications on the parent desktop's bus, or legacy X11
tray items. Keyboard navigation inside the tray uses its native controls.

## Acceptance

In a disposable Mothership session, start with no registrants. Register two
fixtures, change one icon, activate it, and choose an entry from both a root
menu and a submenu. Close one fixture while its menu is open; the other
remains. Close the last and check that the launcher goes away without moving
the shell or affecting Notification Log. Repeat in a Node and check
that Home and the Node see the same items. The Node tray uses the existing
live scene-glass slot, never at the same time as Notification Log.
