#include "XwaylandExpose.h"

void XwaylandForceExpose(Widget w, bool include_children)
{
    if (!w || !XtIsRealized(w) || !XtIsManaged(w))
        return;

    Dimension width = 0;
    Dimension height = 0;

    XtVaGetValues(w,
                  XtNwidth,  &width,
                  XtNheight, &height,
                  nullptr);

    if (width == 0 || height == 0)
        return;

    Display *dpy = XtDisplay(w);
    Window   win = XtWindow(w);
    if (!win)
        return;

    // Force full repaint of this widget
    XClearArea(dpy, win, 0, 0, 0, 0, True);

    if (!include_children || !XtIsComposite(w))
        return;

    WidgetList children = nullptr;
    Cardinal   nchildren = 0;

    XtVaGetValues(w,
                  XmNchildren,    &children,
                  XmNnumChildren, &nchildren,
                  nullptr);

    for (Cardinal i = 0; i < nchildren; ++i)
    {
        Widget c = children[i];
        if (!c || !XtIsRealized(c))
            continue;

        Window cwin = XtWindow(c);
        if (!cwin)
            continue;

        XClearArea(dpy, cwin, 0, 0, 0, 0, True);
    }
}

void XwaylandConfigureEH(Widget w, XtPointer client_data,
                         XEvent *ev, Boolean *cont)
{
    (void)client_data;
    (void)cont;

    if (!ev || ev->type != ConfigureNotify)
        return;

    // For now we always include direct children.
    XwaylandForceExpose(w, true);
}

