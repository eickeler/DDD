// $Id$ -*- C++ -*-
// Setup DDD fonts

// Copyright (C) 1998 Technische Universitaet Braunschweig, Germany.
// Copyright (C) 2003 Free Software Foundation, Inc.
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

char fonts_rcsid[] = 
    "$Id$";

#include "config.h"
#include "fonts.h"
#include "x11/charsets.h"

#include "AppData.h"
#include "x11/DestroyCB.h"
#include "agent/LiterateA.h"
#include "template/StringSA.h"
#include "motif/TextSetS.h"
#include "assert.h"
#include "x11/converters.h"
#include "base/cook.h"
#include "ddd.h"
#include "x11/events.h"
#include "shell.h"
#include "status.h"
#include "base/strclass.h"
#include "string-fun.h"
#include "post.h"
#include "agent/TimeOut.h"

#include <stdlib.h>		// atoi()
#include <ctype.h>
#include <Xm/TextF.h>
#include <Xm/Text.h>
#include <Xm/PushB.h>
#include <X11/Xatom.h>		// XA_...

#include <fontconfig/fontconfig.h>

#include <algorithm>

//-----------------------------------------------------------------------------
// Create an Xft font name
//-----------------------------------------------------------------------------

static void title(const AppData& ad, const string& s)
{
    if (!ad.show_fonts)
	return;

    static bool title_seen = false;

    if (title_seen)
	std::cout << "\n\n";

    std::cout << s << "\n" << replicate('-', s.length()) << "\n\n";

    title_seen = true;
}

static void get_derived_sizes(Dimension size,
			      Dimension& small_size,
			      Dimension& tiny_size,
			      Dimension& llogo_size)
{
    // size i pixels
    small_size = (size * 8) / 9;
    tiny_size  = (size * 6) / 9;
    llogo_size = (size * 3) / 2;
}

//-----------------------------------------------------------------------------
// Setup XFT fonts
//-----------------------------------------------------------------------------

static void setup_xft_fonts(AppData& ad, XrmDatabase& db)
{
    if (ad.fixed_width_font_size >=80 || ad.fixed_width_font_size < 6)
        ad.fixed_width_font_size = 11; // size seem to be in points -> set default

    if (ad.variable_width_font_size>=80 || ad.variable_width_font_size < 6)
        ad.variable_width_font_size = 11; // size seem to be in points -> set default

    // according to hints from Joe Nelson
    XrmPutLineResource(&db, "Ddd*gdb_w.renderTable: tt");
    XrmPutLineResource(&db, "Ddd*help_area*text.renderTable: tt");
    XrmPutLineResource(&db, "Ddd*renderTable: rm,tt,llogo,logo,small,tb,key,bf,sl,bs,accel");

    // the font configuration is moved to the functions CvtStringToXmRenderTable() and make_rendition()
    // in x11/converters.C to work around a bug in the Motif library.
    //
    // XrmPutLineResource(&db, "Ddd*tt*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*tt*fontName: ") + ad.fixed_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*tt*fontSize: ") + itostring(ad.fixed_width_font_size)).chars());
    //
    // XrmPutLineResource(&db, "Ddd*tb*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*tb*fontName: ") + ad.fixed_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*tb*fontSize: ") + itostring(ad.fixed_width_font_size)).chars());
    // XrmPutLineResource(&db, "Ddd*tb*fontStyle: Bold");
    //
    //
    // XrmPutLineResource(&db, "Ddd*rm*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*rm*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*rm*fontSize: ") + itostring(ad.variable_width_font_size)).chars());
    //
    // XrmPutLineResource(&db, "Ddd*bf*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*bf*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*bf*fontSize: ") + itostring(ad.variable_width_font_size)).chars());
    // XrmPutLineResource(&db, "Ddd*bf*fontStyle: Bold");
    //
    // XrmPutLineResource(&db, "Ddd*sl*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*sl*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*sl*fontSize: ") + itostring(ad.variable_width_font_size)).chars());
    // XrmPutLineResource(&db, "Ddd*sl*fontStyle: Bold"); // Oblique fonts do not work in Motif
    //
    // XrmPutLineResource(&db, "Ddd*bs*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*bs*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*bs*fontSize: ") + itostring(ad.variable_width_font_size)).chars());
    // XrmPutLineResource(&db, "Ddd*bs*fontStyle: Bold"); // combination of Bold and Oblique not possible in Motif
    //
    // XrmPutLineResource(&db, "Ddd*small*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*small*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*small*fontSize: ") + itostring(ad.variable_width_font_size*.8)).chars());
    //
    // XrmPutLineResource(&db, "Ddd*llogo*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*llogo*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*llogo*fontSize: ") + itostring(ad.variable_width_font_size*2)).chars());
    // XrmPutLineResource(&db, (string("Ddd*llogo*fontStyle: Bold").chars()));
    //
    // XrmPutLineResource(&db, "Ddd*logo*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*logo*fontName: ") + ad.variable_width_font).chars() );
    // XrmPutLineResource(&db, (string("Ddd*logo*fontSize: ") + itostring(ad.variable_width_font_size*1.2)).chars());
    // XrmPutLineResource(&db, "Ddd*logo*fontStyle: Bold");
    //
    // XrmPutLineResource(&db, "Ddd*key*fontType: FONT_IS_XFT");
    // XrmPutLineResource(&db, (string("Ddd*key*fontName: ") + ad.variable_width_font).chars());
    // XrmPutLineResource(&db, (string("Ddd*key*fontSize: ") + itostring(ad.variable_width_font_size)).chars());
    // XrmPutLineResource(&db, "Ddd*key*fontStyle: Bold");
    //
    // if (ad.retro_style)
    // {
    //     XrmPutLineResource(&db, "Ddd*accel*fontType: FONT_IS_XFT");
    //     XrmPutLineResource(&db, (string("Ddd*accel*fontName: ") + ad.variable_width_font).chars());
    //     XrmPutLineResource(&db, (string("Ddd*accel*fontSize: ") + itostring(ad.variable_width_font_size)).chars());
    // }
    // else
    // {
    //     XrmPutLineResource(&db, "Ddd*accel*fontType: FONT_IS_XFT");
    //     XrmPutLineResource(&db, (string("Ddd*accel*fontName: ") + ad.fixed_width_font).chars());
    //     XrmPutLineResource(&db, (string("Ddd*accel*fontSize: ") + itostring(ad.variable_width_font_size*0.8)).chars());
    // }
}

string make_xftfont(const AppData& ad, DDDFont base)
{
    switch(base)
    {
        case VariableWidthDDDFont:
            return string(ad.variable_width_font) +string(":size=") + itostring(ad.variable_width_font_size);

        case DataDDDFont:
            return string(ad.data_font) +string(":size=") + itostring(ad.data_font_size);

        default:
        case FixedWidthDDDFont:
            return string(ad.fixed_width_font) +string(":size=") + itostring(ad.fixed_width_font_size);
    }
}

//-----------------------------------------------------------------------------
// Set VSL font resources
//-----------------------------------------------------------------------------

void replace_vsl_xftfont(string& defs, const string& func,
			     const string& font, const Dimension size, const string& override = "")
{
    string fontname = quote(font+string(":size=")+itostring(size)+override);
    defs += "#pragma replace " + func + "\n" + 
	func + "(box) = font(box, " + fontname + ");\n";
}

static void setup_vsl_fonts(AppData& ad)
{
    Dimension small_size, tiny_size, llogo_size;
    if (ad.data_font_size >=80 || ad.data_font_size < 6)
        ad.data_font_size = 11;

    get_derived_sizes(ad.data_font_size, small_size, tiny_size, llogo_size);

    static string defs; // defs.chars() is used in AppData
    defs = "";

    title(ad, "VSL defs");

    replace_vsl_xftfont(defs, "rm", ad.data_font, ad.data_font_size);
    replace_vsl_xftfont(defs, "bf", ad.data_font, ad.data_font_size, ":weight=bold");
    replace_vsl_xftfont(defs, "it", ad.data_font, ad.data_font_size, ":slant=italic");
    replace_vsl_xftfont(defs, "bf", ad.data_font, ad.data_font_size, ":weight=bold:slant=italic");

    replace_vsl_xftfont(defs, "small_rm", ad.data_font, small_size);
    replace_vsl_xftfont(defs, "small_bf", ad.data_font, small_size, ":weight=bold");
    replace_vsl_xftfont(defs, "small_it", ad.data_font, small_size, ":slant=italic");
    replace_vsl_xftfont(defs, "small_bf", ad.data_font, small_size, ":weight=bold:slant=italic");

    replace_vsl_xftfont(defs, "tiny_rm", ad.data_font, tiny_size);
    replace_vsl_xftfont(defs, "tiny_bf", ad.data_font, tiny_size, ":weight=bold");
    replace_vsl_xftfont(defs, "tiny_it", ad.data_font, tiny_size, ":slant=italic");
    replace_vsl_xftfont(defs, "tiny_bf", ad.data_font, tiny_size, ":weight=bold:slant=italic");

    if (ad.show_fonts)
	std::cout << defs;

    defs += ad.vsl_base_defs;
    ad.vsl_base_defs = defs.chars();
}

void setup_fonts(AppData& ad, XrmDatabase db)
{
    XrmDatabase db2 = db;
    setup_xft_fonts(ad, db2);
    assert(db == db2);

    setup_vsl_fonts(ad);
}



//-----------------------------------------------------------------------------
// Handle font resources
//-----------------------------------------------------------------------------

// Set a new font resource
void set_font(DDDFont font, const string& name)
{
    switch (font)
    {
    case DefaultDDDFont:
    {
	static string s;
	s = name;
	app_data.default_font = s.chars();
	break;
    }
    case VariableWidthDDDFont:
    {
	static string s;
	s = name;
	app_data.variable_width_font = s.chars();
	break;
    }
    case FixedWidthDDDFont:
    {
	static string s;
	s = name;
	app_data.fixed_width_font = s.chars();
	break;
    }
    case DataDDDFont:
    {
	static string s;
	s = name;
	app_data.data_font = s.chars();
	break;
    }
    default:
	assert(0);
	::abort();
    }
}

// Set a new font resource
static void set_font_size(DDDFont font, int size)
{
    switch (font)
    {
    case DefaultDDDFont:
	app_data.default_font_size = size;
	break;
    case VariableWidthDDDFont:
	app_data.variable_width_font_size = size;
	break;
    case FixedWidthDDDFont:
	app_data.fixed_width_font_size = size;
	break;
    case DataDDDFont:
	app_data.data_font_size = size;
	break;
    default:
	assert(0);
	::abort();
    }
}


void SetFontNameCB(Widget w, XtPointer client_data, XtPointer)
{
    DDDFont font = (DDDFont) (long) client_data;
    String s = XmTextFieldGetString(w);

    if (s == nullptr || s[0] == 0)
    {
        XtFree(s);
        return;
    }

    set_font(font, s);
    XtFree(s);

    update_reset_preferences();
}

void SetFontSizeCB(Widget w, XtPointer client_data, XtPointer)
{
    DDDFont font = (DDDFont) (long) client_data;
    String s = XmTextFieldGetString(w);

    int size = 0;
    if (s != nullptr)
        size = atoi(s);
    XtFree(s);

    if (size < 6 || size >= 80)
        return;                                 // keep previous value

    set_font_size(font, size);
    update_reset_preferences();
}

std::vector<string> GetFixedWithFonts()
{
    std::vector<string> fontlist;

    FcInit();

    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_SPACING, FC_CHARSET, NULL);

    FcFontSet *fontSet = FcFontList(NULL, pattern, os);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pattern);

    if (fontSet == nullptr)
        return fontlist;

    const FcCharSet *englishCharset = FcLangGetCharSet((const FcChar8 *)"en");
    for (int i = 0; i < fontSet->nfont; ++i)
    {
        FcPattern *font = fontSet->fonts[i];
        FcChar8 *family, *style ;
        int spacing;
        FcCharSet *charset;

        if (FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch &&
            FcPatternGetString(font, FC_STYLE, 0, &style) == FcResultMatch &&
            FcPatternGetInteger(font, FC_SPACING, 0, &spacing) == FcResultMatch &&
            FcPatternGetCharSet(font, FC_CHARSET, 0, &charset) == FcResultMatch &&
            (spacing == FC_MONO || spacing == FC_DUAL) &&
            (strcmp((char*)style, "Medium") == 0 || strcmp((char*)style, "Regular") == 0) &&
            (FcCharSetIsSubset(englishCharset, charset)))
        {
            fontlist.push_back(string((char*)family));
        }
    }

    std::sort(fontlist.begin(), fontlist.end());

    // remove duplicates
    fontlist.erase(std::unique(fontlist.begin(), fontlist.end()), fontlist.end());

    FcFontSetDestroy(fontSet);

    return fontlist;
}

std::vector<string> GetVariableWithFonts()
{
    std::vector<string> fontlist;

    FcInit();

    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_SPACING, FC_CHARSET, NULL);

    FcFontSet *fontSet = FcFontList(NULL, pattern, os);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pattern);

    if (!fontSet)
        return fontlist;

    const FcCharSet *englishCharset = FcLangGetCharSet((const FcChar8 *)"en");
    for (int i = 0; i < fontSet->nfont; ++i)
    {
        FcPattern *font = fontSet->fonts[i];
        FcChar8 *family, *style;
        int spacing;
        FcCharSet *charset;

        if (FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch &&
            FcPatternGetString(font, FC_STYLE, 0, &style) == FcResultMatch &&
            FcPatternGetInteger(font, FC_SPACING, 0, &spacing) == FcResultNoMatch &&
            FcPatternGetCharSet(font, FC_CHARSET, 0, &charset) == FcResultMatch &&
            (strcmp((char*)style, "Medium") == 0 || strcmp((char*)style, "Regular") == 0) &&
            (FcCharSetIsSubset(englishCharset, charset)))
        {
            fontlist.push_back(string((char*)family));
        }
    }

    std::sort(fontlist.begin(), fontlist.end());

    // remove duplicates
    fontlist.erase(std::unique(fontlist.begin(), fontlist.end()), fontlist.end());

    FcFontSetDestroy(fontSet);

    return fontlist;
}

