// $Id$
// Font tables

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

char FontTable_rcsid[] = 
    "$Id$";

#include "base/assert.h"
#include "base/hash.h"
#include "base/strclass.h"

#include <iostream>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#include "FontTable.h"

DEFINE_TYPE_INFO_0(FontTable)

// FontTable

// Return hash code
inline unsigned hash(const char *name)
{
    return hashpjw(name) % MAX_FONTS;
}

// Return XFontStruct for given font name NAME
BoxFont *FontTable::operator[](const string& name)
{
    int i = hash(name.chars());
    while (table[i].font != 0 && name != table[i].name)
    {
	assert (i < MAX_FONTS);   // Too many fonts
	i = (i >= MAX_FONTS) ? 0 : i + 1;
    }

    if (table[i].font == 0 && name != table[i].name)
    {
	// Insert new font
	table[i].name = name;
 	table[i].font = XftFontOpenName(_display, DefaultScreen(_display), (name+":antialias=true").chars());
	if (table[i].font == 0)
	{
	    std::cerr << "Warning: Could not load font \"" << name << "\"";

	    // Try default font
            table[i].font = XftFontOpen(_display, DefaultScreen(_display), XFT_FAMILY, XftTypeString, "", NULL);
            std::cerr << ", using default font instead\n";
        }
    }

    return table[i].font;
}
