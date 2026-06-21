#ifndef XWAYLANDEXPOSE_H
#define XWAYLANDEXPOSE_H

#include <Xm/Xm.h>
#include <X11/Intrinsic.h>
#include <X11/Xlib.h>

// Force a full repaint of W and (optionally) all direct children.
// This is used as a workaround for Xwayland not sending reliable Expose
// events when widget geometry changes.
void XwaylandForceExpose(Widget w, bool include_children = true);

// Generic event handler wrapper for ConfigureNotify on a widget.
// client_data can be cast from a bool* or just left NULL; we currently
// always include children.
void XwaylandConfigureEH(Widget w, XtPointer client_data,
                         XEvent *ev, Boolean *cont);

#endif // XWAYLANDEXPOSE_H
