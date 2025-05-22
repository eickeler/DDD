// $Id$ -*- C++ -*-
// Create an XImage from bitmap data

// Copyright (C) 1998-1999 Technische Universitaet Braunschweig, Germany.
// Written by Andreas Zeller <zeller@gnu.org>.
// 
// This file is part of DDD.
// 
// DDD is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or (at your option) any later version.
// 
// DDD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public
// License along with DDD -- see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
// 
// DDD is the data display debugger.
// For details, see the DDD World-Wide-Web page, 
// `http://www.gnu.org/software/ddd/',
// or send a mail to the DDD developers <ddd@gnu.org>.

char InitImage_rcsid[] = 
    "$Id$";

#include "InitImage.h"

#include "ddd.h"

#include "config.h"
#include "base/assert.h"
#include "base/casts.h"
#include "AppData.h"

// These three are required for #including <X11/Xlibint.h>
#include <string.h>		// bcopy()
#include <sys/types.h>		// size_t
#include <X11/Xlib.h>		// anything else

#include <X11/Xlibint.h>	// Xcalloc()
#include <X11/Xutil.h>		// XGetPixel(), etc.

#include <Xm/Xm.h>		// XmInstallImage()


XImage *CreateImageFromBitmapData(unsigned char *bits, int width, int height)
{
    XImage *image = (XImage *)Xcalloc(1, sizeof(XImage));
    image->width            = width;
    image->height           = height;
    image->xoffset          = 0;
    image->format           = XYBitmap;
    image->data             = (char *) bits;
    image->byte_order       = MSBFirst;
    image->bitmap_unit      = 8;
    image->bitmap_bit_order = LSBFirst;
    image->bitmap_pad       = 8;
    image->depth            = 1;
    image->bytes_per_line   = (width + 7) / 8;

    XInitImage(image);

    return image;
}

// Install the given X bitmap as NAME
Boolean InstallBitmap(unsigned char *bits, int width, int height, const char *name)
{
    XImage *image = CreateImageFromBitmapData(bits, width, height);
    return XmInstallImage(image, XMST(name));
}


// Install the given X bitmap as full color image to prevent color changes
Boolean InstallBitmapAsXImage(Widget w, unsigned char *bits, int width, int height, const char *name, int scalefactor)
{
    Display *display = XtDisplay(toplevel);
    XrmDatabase db = XtDatabase(display);
    string resource = string(name) + ".foreground";

    string str_name  = "ddd*" + resource;
    string str_class = "Ddd*" + resource;

    char *type;
    XrmValue xrmvalue;
    Bool ok = XrmGetResource(db, str_name.chars(), str_class.chars(), &type, &xrmvalue);
    string fg;
    if (ok)
    {
        const char *str = (const char *)xrmvalue.addr;
        int len   = xrmvalue.size - 1; // includes the final `\0'
        fg = string(str, len);
        printf("%s.foreground: %s\n", name, fg.chars());
    }

    ok = XrmGetResource(db, "ddd*XmText.background", "Ddd*XmText.background", &type, &xrmvalue);
    string bg;
    if (ok)
    {
        const char *str = (const char *)xrmvalue.addr;
        int len   = xrmvalue.size - 1; // includes the final `\0'
        bg = string(str, len);
        printf("XmText.background: %s\n", bg.chars());
    }

    Colormap colormap = DefaultColormap(display, DefaultScreen(display));

    XColor colorfg, colorfgx;
    XAllocNamedColor(display, colormap, fg.chars(), &colorfg, &colorfgx);

    XColor colorbg, colorbgx;
    XAllocNamedColor(display, colormap, bg.chars(), &colorbg, &colorbgx);

    if (app_data.dark_mode)
    {
        // brighten color of glyphs in dark mode
        colorfgx.pixel += 0x00202020;

        // invert background
        colorbgx.pixel ^= 0xffffff;
    }

    Visual *visual = XDefaultVisual(display, XDefaultScreen(display));
    int depth = XDefaultDepth(display, XDefaultScreen(display));

    int scaledwidth = scalefactor * width;
    int scaledheight = scalefactor * height;
    XImage *image = XCreateImage(XtDisplay(w), visual, depth, ZPixmap, 0, nullptr,
                                    scaledwidth, scaledheight, 32, 0);
    image->data = (char *)malloc(image->height * image->bytes_per_line);

    int bytes_per_line = (width + 7) / 8;
    for (int y = 0; y < image->height; y++)
    {
        for (int x = 0; x < image->width; x++)
        {
            int xi = x / scalefactor;
            int yi = y / scalefactor;
            int pixel = (bits[yi*bytes_per_line + xi/8]>>(xi&0x7)) & 0x01;
            if (pixel)
                XPutPixel(image, x, y, colorfgx.pixel);
            else
                XPutPixel(image, x, y, colorbgx.pixel);
        }
    }

    return XmInstallImage(image, XMST(name));
}
