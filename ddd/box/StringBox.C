// $Id$ 
// StringBox class

// Copyright (C) 1995 Technische Universitaet Braunschweig, Germany.
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

char StringBox_rcsid[] = 
    "$Id$";

#include "StringBox.h"
#include "printBox.h"

#include "base/strclass.h"
#include "base/assert.h"
#include "base/cook.h"

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <ctype.h>
#include <sstream>

#include "vslsrc/VSEFlags.h"

// set this to 1 if you want to have the box a font-specific height
#define USE_MAX_BOUNDS 1


DEFINE_TYPE_INFO_1(StringBox, PrimitiveBox)

// StringBox

FontTable *StringBox::fontTable = 0;
bool StringBox::quoted = false;
float StringBox::scale = 1.0f;

// Recompute size
Box *StringBox::resize()
{
    if (m_font != 0)
    {
        XGlyphInfo extents;
	XftTextExtentsUtf8(fontTable->getDisplay(), m_font, (const FcChar8*)m_string.chars(), m_string.length(), &extents);
        m_ascent = m_font->ascent;
        thesize() = BoxSize(extents.width, m_font->height);
    }

    return this;
}

// Draw
void StringBox::_draw(Widget w, 
		      const BoxRegion& r, 
		      const BoxRegion&, 
		      GC gc,
		      bool) const
{
    BoxPoint origin = r.origin();
    Visual *visual = DefaultVisual(XtDisplay(w), DefaultScreen(XtDisplay(w)));
    Colormap cmap = DefaultColormap(XtDisplay(w),  DefaultScreen(XtDisplay(w)));
    XftDraw *draw = XftDrawCreate(XtDisplay(w), XtWindow(w), visual, cmap);

    XGCValues gc_values;
    XGetGCValues(XtDisplay(w), gc, GCForeground, &gc_values);

    XftColor color;
    XColor xcol;
    xcol.pixel = gc_values.foreground;
    XQueryColor(XtDisplay(w), DefaultColormap(XtDisplay(w), DefaultScreen(XtDisplay(w))), &xcol);
    color.color.red = xcol.red;
    color.color.blue = xcol.blue;
    color.color.green = xcol.green;
    color.color.alpha = 0xFFFF;

    XftDrawStringUtf8(draw, &color, m_font, origin[X], origin[Y] + m_ascent, (const FcChar8*)m_string.chars(), m_string.length());
    XftColorFree(XtDisplay(w), visual, cmap, &color);
    XftDrawDestroy(draw);
}


void StringBox::dump(std::ostream& s) const
{
    const char *quote = "\"";
    if (StringBox::quoted)
	quote = "\\\"";

    s << quote;
    for (unsigned i = 0; i < m_string.length(); i++)
    {
	if (m_string[i] == '\"')
	    s << quote;
	else
	    s << m_string[i];
    }
    s << quote;

    if (VSEFlags::include_font_info)
	s << " (font: \"" << m_fontname << "\")";
}

void StringBox::newFont(const string& fontname)
{
    m_fontname = fontname;

    // extract base size
    string sizestr = m_fontname.after(":size=");
    if (!sizestr.empty())
    {
        if (sizestr.contains(":"))
            sizestr = sizestr.before(":");
        m_basefontsize = atof(sizestr.chars());
    }

    int pos = m_fontname.index(":size=");
    if (pos>=0)
    {
        float target = scale * m_basefontsize;
        string newfont = m_fontname.at(0, pos+6);
        char sizebuf[32];
        snprintf(sizebuf, sizeof(sizebuf), "%.1f", target);
        newfont += sizebuf;

        int pos2 = m_fontname.index(":", pos+5);
        if (pos2>0)
            newfont += m_fontname.after(pos2-1);
        m_fontname = newfont;
    }


    if (fontTable != 0)
	_newFont((*fontTable)[m_fontname]);
}

// Print
void StringBox::_print(std::ostream& os,
		       const BoxRegion& region, 
		       const PrintGC& gc) const
{
    // Don't draw empty strings
    if (str().empty())
	return;

    BoxPoint origin = region.origin() ;

    if (gc.isFig()) {
	os << TEXTHEAD1 << 12 << " "
	   << size(Y) - 3 << " " << TEXTHEAD2
	   << size(X) << " " << size(Y) << " "
	   << origin[X] << " " << origin [Y] + size(Y) - 2 << " "
	   << str() << "\001\n";
    } else if (gc.isPostScript()) {
	os << "/Courier" << " " << size(X) << " " << size(Y)
	   << " " << origin[X] << " " << origin[Y] + size(Y) << " "
	   << "(" << pscook(str()) << ") text*\n";
    }
}   
