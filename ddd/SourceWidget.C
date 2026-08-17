// SourceWidget.cpp
/*!
 * \file
 * \brief XmPrimitive-based, colored, static text viewer for read-only source display.
 *
 * This module implements a lightweight replacement for the Motif XmText widget to render
 * read-only, syntax-colored text using Xft. It provides a real Xt/Motif widget
 * (XmPrimitive subclass) so callers can use XtQueryGeometry and XtVaSetValues directly.
 *
 * It places the widget inside an XmScrolledWindow (CreateSVScrolledText),
 * measures text with Xft for pixel-accurate positioning, supports selection and a caret,
 * and exposes helpers to map between byte positions and pixel coordinates.
 *
 * Notes:
 * - Positions are byte offsets in UTF-8 (not code points or columns).
 * - Rendering relies on Xft; font defaults to "monospace-11".
 * - Scrolling is managed via the XmScrolledWindow scrollbars.
 */

#include "SourceWidget.h"
#include <Xm/Xm.h>
#include <Xm/Text.h>
#include <Xm/ScrolledW.h>
#include <Xm/PrimitiveP.h>
#include <Xm/ScrollBar.h>
#include <Xm/Form.h>
#include <Xm/CutPaste.h>
#include <X11/Xft/Xft.h>
#include <X11/IntrinsicP.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <fontconfig/fontconfig.h>
#include <wchar.h>

#include <ctype.h>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Forward decls of handlers used by the widget class
static void buttonEH(Widget, XtPointer client, XEvent* ev, Boolean* cont);
static void keyEH(Widget, XtPointer client, XEvent *ev, Boolean *cont);
static void draw_expose(struct CtvCtx *ctx, XExposeEvent *ex);
static void destroyCB(Widget w, XtPointer client, XtPointer call);
static void compute_lines(struct CtvCtx *ctx);
static void queue_redraw(struct CtvCtx *ctx);

// Action function declarations
static void GrabFocusAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void ExtendEndAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void SelectAllAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void EndOfLineAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void NextPageAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void PreviousPageAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void CopyClipboardAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void EmptyAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void BeginningOfLineAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void PreviousLineAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void NextLineAction(Widget w, XEvent *event, String *params, Cardinal *num_params);

//=====================================================
// XmhColorTextView widget class (XmPrimitive subclass)
//=====================================================
typedef struct {
    int dummy;// class extension placeholder
} CtvTextClassPart;

typedef struct _CtvTextClassRec {
    CoreClassPart        core_class;
    XmPrimitiveClassPart primitive_class;
    CtvTextClassPart     ctvtext_class;
} CtvTextClassRec;

typedef struct {
    // Resources
    String string;      // XmhNstring
    String fontFamily;  // XmhNfontFamily
    int    fontSize;    // XmhNfontSize
    int    columns;     // XmhNcolumns
    int    rows;        // XmhNrows

    // Internal
    CtvCtx *ctx;
    XtCallbackList gain_primary_callback;
    XtCallbackList viewport_changed_callback;
} CtvTextPart;

typedef struct _CtvTextRec {
    CorePart        core;
    XmPrimitivePart primitive;
    CtvTextPart     ctvtext;
} CtvTextRec;

// Resource list
static XtResource sv_resources[] = {
    { XmhNstring,     (char*)"String",     XmRString, sizeof(String),
      XtOffsetOf(CtvTextRec, ctvtext.string),     XmRString,   (XtPointer)"" },

    { XmhNfontFamily, (char*)"FontFamily", XmRString, sizeof(String),
      XtOffsetOf(CtvTextRec, ctvtext.fontFamily), XmRString,   (XtPointer)"monospace" },

    { XmhNfontSize,   (char*)"FontSize",   XtRInt,  sizeof(int),
      XtOffsetOf(CtvTextRec, ctvtext.fontSize),   XmRImmediate, (XtPointer)11 },

    { XmhNcolumns,    (char*)"Columns",    XtRInt,    sizeof(int),
      XtOffsetOf(CtvTextRec, ctvtext.columns),    XmRImmediate, (XtPointer)80 },

    { XmhNrows,       (char*)"Rows",       XtRInt,    sizeof(int),
      XtOffsetOf(CtvTextRec, ctvtext.rows),       XmRImmediate, (XtPointer)31 },

    { XmNgainPrimaryCallback, (char*)"GainPrimaryCallback", XmRCallback, sizeof(XtCallbackList),
      XtOffsetOf(CtvTextRec, ctvtext.gain_primary_callback), XmRCallback, (XtPointer)NULL },

    { XmhNviewportChangedCallback, (char*)"ViewportChangedCallback", XmRCallback, sizeof(XtCallbackList),
      XtOffsetOf(CtvTextRec, ctvtext.viewport_changed_callback), XmRCallback, (XtPointer)NULL },
};

static XtActionsRec ctv_actions[] = {
    { (char*)"grab-focus",  GrabFocusAction },
    { (char*)"extend-end",  ExtendEndAction },
    { (char*)"select-all",  SelectAllAction },
    { (char*)"end-of-line", EndOfLineAction },
    { (char*)"next-page",   NextPageAction },
    { (char*)"previous-page",   PreviousPageAction },
    { (char*)"cut-clipboard", CopyClipboardAction },
    { (char*)"copy-clipboard", CopyClipboardAction },
    { (char*)"paste-clipboard", EmptyAction },
    { (char*)"toggle-overstrike", EmptyAction },
    { (char*)"beginning-of-line", BeginningOfLineAction },
    { (char*)"delete-next-character", EmptyAction },
    { (char*)"delete-previous-character", EmptyAction },
    { (char*)"delete-to-end-of-line", EmptyAction },
    { (char*)"delete-previous-word", EmptyAction },
    { (char*)"previous-line",   PreviousLineAction },
    { (char*)"next-line",   NextLineAction },
    { (char*)"newline",     EmptyAction }
    // Add more actions as needed
};

// Forward class methods
static void    Initialize(Widget req, Widget w, ArgList args, Cardinal *num);
static void    Destroy(Widget w);
static Boolean SetValues(Widget old, Widget req, Widget nw, ArgList args, Cardinal *num);
static void    DoExpose(Widget w, XEvent *event, Region region);
static XtGeometryResult QueryGeometry(Widget w, XtWidgetGeometry *in, XtWidgetGeometry *out);

// Class record
static CtvTextClassRec XmhColorTextViewClassRec = {
    // CoreClassPart
    {
        (WidgetClass)&xmPrimitiveClassRec,   // superclass
        (char*)"XmhColorTextView",                    // class_name
        sizeof(CtvTextRec),                  // widget_size
        NULL,                                // class_initialize
        NULL,                                // class_part_initialize
        False,                               // class_inited
        Initialize,                          // initialize
        NULL,                                // initialize_hook
        XtInheritRealize,                    // realize (core will create window)
        ctv_actions,                          // actions
        XtNumber(ctv_actions),                // num_actions
        sv_resources,                        // resources
        (Cardinal)(sizeof(sv_resources)/sizeof(sv_resources[0])),
        NULLQUARK,                           // xrm_class
        True,                                // compress_motion
        XtExposeCompressMultiple,            // compress_exposure
        True,                                // compress_enterleave
        False,                               // visible_interest
        Destroy,                             // destroy
        XtInheritResize,                     // resize
        DoExpose,                            // expose
        SetValues,                           // set_values
        NULL,                                // set_values_hook
        XtInheritSetValuesAlmost,            // set_values_almost
        NULL,                                // get_values_hook
        NULL,                                // accept_focus
        XtVersion,                           // version
        NULL,                                // callback_private
        NULL,                                // tm_table
        QueryGeometry,                       // query_geometry
        NULL,                                // display_accelerator
        NULL                                 // extension
    },
    // XmPrimitiveClassPart
    {
        XmInheritBorderHighlight,
        XmInheritBorderUnhighlight,
        NULL,         // translations
        NULL,         // arm_and_activate
        NULL,         // syn resources
        0,            // num syn resources
        NULL          // extension
    },
    // SwTextClassPart
    { 0 }
};

static WidgetClass XmhColorTextViewWidgetClass = (WidgetClass)&XmhColorTextViewClassRec;

bool XmhIsColorTextView(Widget w)
{
    return XtIsSubclass(w, XmhColorTextViewWidgetClass);
}

/*!
 * \brief Internal rendering and state context.
 * \internal
 * Holds content, layout metrics, Xft objects, and widget/interaction state.
 */
typedef struct CtvCtx
{
    // Content
    char *text = nullptr;
    Utf8Pos text_len = 0;
    std::vector<XmhColorToken> tokens;

    GC bg_gc = nullptr;

    // Layout
    int line_count = 0;
    std::vector<Utf8Pos> line_starts;
    int max_line_px = 0;

    int width_px = 0;   // virtual content width (px)
    int height_px = 0;  // virtual content height (Initializepx)
    int viewport_width  = 0;   // actual widget width (px)
    int viewport_height = 0;   // actual widget height (px)
    int gutter_px = 0;
    bool gutter_enabled = true;

    // Xft
    XftDraw *xft = nullptr;
    XftFont *font = nullptr;
    XftFont *font_bold = nullptr;
    XftFont *font_italic = nullptr;
    XftFont *font_bolditalic = nullptr;
    std::vector<XftFont*> font_fallbacks;            // fallback fonts (regular)

    // back buffer
    Pixmap   back_pix = 0;
    int      back_w   = 0;
    int      back_h   = 0;

    char *font_family = nullptr;
    double font_pt = 0.0;
    Visual  *visual = nullptr;
    Colormap cmap = 0;
    Pixel bg = 0;
    int ascent = 0;
    int descent = 0;
    int line_height = 0;
    int cellwidth = 0;

    // View/selection/caret
    Utf8Pos sel_start = 0;
    Utf8Pos sel_end = 0;
    int has_sel = 0;
    Utf8Pos caret = 0;
    int caret_visible = 1;

    // Widgets
    Widget scrolledWindow = 0;
    Widget textWidget = 0; // points to the XmhColorTextView widget itself
    Widget vbar = 0;
    Widget hbar = 0;

    // Interaction
    int dragging = 0;
    Utf8Pos drag_anchor = 0;

    int goal_x = 0;  // goal colums during vertical movement, -1 : invalid
    Utf8Pos sel_anchor = 0;

    // Colors
    std::vector<XftColor> palette;
    XftColor selc = {};
    XftColor caretc = {};
    XftColor gutterc = {};
    bool dark_mode = false;

    // Viewport tracking (last known scrollbar positions)
    int prev_v = -1;
    int prev_h = -1;
} CtvCtx;

const char *defaultnames[] =
{
    "#1f1c1b", // Default
    "#1f1c1b", // Keyword
    "#0057ae", // Type normal types and *_type, *_t
    "#b08000", // Number
    "#bf0303", // String
    "#924c9d", // Char
    "#898887", // Comment
    "#006e28", // Preprocessor
    "#ff5500", // Includes
    "#ca60ca", // Operator
    "#644a9b", // Standard Classes
    "#0095ff", // Boost Stuff
    "#0057ae", // Data Members m_*, Globals g_*, Statics s_*
    "#ca60ca", // Annotation in comment (Doyxgen commands)
    "#ca60ca", // Delimiter , and ;
    "#ca60ca", // Bracket  ( ) { } [ ]
    "#0057ae",  // Hex addresses
    "#bf0303",  // CPU registers
    "#00a000",  // Assembly instructions
    "#b08000"   // Function labels
};

const char *darkdefaultnames[] =
{
    "#cfcfc2", // Default
    "#cfcfc2", // Keyword
    "#27aeae", // Type normal types and *_type, *_t
    "#f67400", // Number
    "#f44f4f", // String
    "#3daee9", // Char
    "#7a7c7d", // Comment
    "#27ad60", // Preprocessor
    "#27ad60", // Includes
    "#3f8058", // Operator
    "#644a9b", // Standard Classes
    "#0095ff", // Boost Stuff
    "#27aeae", // Data Members m_*, Globals g_*, Statics s_*
    "#3f8058", // Annotation in comment (Doyxgen commands)
    "#3f8058", // Delimiter , and ;
    "#3f8058", // Bracket  ( ) { } [ ]
    "#27aeae",  // Hex addresses
    "#f44f4f",  // CPU registers
    "#00c000",  // Assembly instructions
    "#c0a000"   // Function labels
};



static void free_palette(Display *dpy, CtvCtx *ctx);
static void set_hscroll(CtvCtx *ctx, int x);
static void free_text(CtvCtx *ctx);
static Utf8Pos line_end_no_nl(CtvCtx *ctx, int li);
static void update_metrics_from_font(CtvCtx *ctx);


typedef enum {
    CTV_COORD_ABSOLUTE,   // relative to (0,0) of the widget
    CTV_COORD_VIEWPORT    // relative to the top-left of the visible area
} CtvCoordMode;

static int pos_to_xy(CtvCtx *ctx, Utf8Pos p, int *x, int *y, CtvCoordMode mode);
static Utf8Pos xy_to_pos(CtvCtx *ctx, int x, int y, CtvCoordMode mode);

static int get_visible_lines(CtvCtx *ctx);
static int topLine_from_scroll(CtvCtx *ctx, int line_count, int visible_lines);
static void scroll_from_topLine(CtvCtx *ctx, int topLine,
                                    int line_count, int visible_lines);
static void update_scrollbars(CtvCtx *ctx);
static void vscrollCB(Widget w, XtPointer client, XtPointer call);
static void hscrollCB(Widget w, XtPointer client, XtPointer call);
static void configureEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont);

/*! \brief Retrieve the internal context from XmhColorTextViewWidgetClass widget. */
static CtvCtx* get_ctx(Widget w)
{
    if (!w)
        return nullptr;

    if (!XtIsSubclass(w, XmhColorTextViewWidgetClass))
        return nullptr;

    CtvTextRec *tw = (CtvTextRec*)w;
    return tw->ctvtext.ctx;
}

// Helper to free styled faces
static void free_styled_fonts(Display *dpy, CtvCtx *ctx)
{
    if (!ctx || !dpy)
        return;
    if (ctx->font_bold)
    {
        XftFontClose(dpy, ctx->font_bold);
        ctx->font_bold = NULL;
    }

    if (ctx->font_italic)
    {
        XftFontClose(dpy, ctx->font_italic);
        ctx->font_italic = NULL;
    }

    if (ctx->font_bolditalic)
    {
        XftFontClose(dpy, ctx->font_bolditalic);
        ctx->font_bolditalic = NULL;
    }
}

static void clear_fallback_fonts(CtvCtx *ctx)
{
    if (!ctx)
        return;
    Display *dpy = XtDisplayOfObject(ctx->textWidget);
    if (!dpy)
        return;

    for (XftFont *fb : ctx->font_fallbacks)
        if (fb)
            XftFontClose(dpy, fb);

    ctx->font_fallbacks.clear();
}

static void destroyCB(Widget, XtPointer client, XtPointer)
{
    CtvCtx *ctx = (CtvCtx*)client;
    if (!ctx)
        return;

    Display *dpy = XtDisplay(ctx->textWidget);

    free_text(ctx);
    free_styled_fonts(dpy, ctx);

    if (ctx->font)
    {
        XftFontClose(dpy, ctx->font);
        ctx->font = nullptr;
    }

    clear_fallback_fonts(ctx);

    // free GC
    if (ctx->bg_gc)
    {
        XFreeGC(dpy, ctx->bg_gc);
        ctx->bg_gc = nullptr;
    }

    // free back buffer
    if (ctx->xft)
    {
        XftDrawDestroy(ctx->xft);
        ctx->xft = nullptr;
    }

    if (ctx->back_pix)
    {
        XFreePixmap(dpy, ctx->back_pix);
        ctx->back_pix = 0;
    }

    free_palette(dpy, ctx);

    free((char*)ctx->font_family);
    delete ctx;
}
/*!
 * \internal
 * \brief Allocate a small default Xft palette.
 */
static void alloc_default_palette(Display *dpy, Visual *vis, Colormap cmap, CtvCtx *ctx)
{
    if (!ctx->palette.empty())
        return;

    ctx->palette.resize((sizeof(defaultnames)/sizeof(defaultnames[0])));
    if (ctx->dark_mode)
        for (size_t i=0; i<ctx->palette.size(); i++)
            XftColorAllocName(dpy, vis, cmap, darkdefaultnames[i], &ctx->palette[i]);
    else
        for (size_t i=0; i<ctx->palette.size(); i++)
            XftColorAllocName(dpy, vis, cmap, defaultnames[i], &ctx->palette[i]);

    // selection
    XRenderColor rc;
    rc.red   = 0x94ff;
    rc.green = 0xcaff;
    rc.blue  = 0xefff;
    rc.alpha = 0x8ccc;
    XftColorAllocValue(dpy, ctx->visual, ctx->cmap, &rc, &ctx->selc);

    // caret
    if (ctx->dark_mode)
        XftColorAllocName(dpy, vis, cmap, "#cfcfc2", &ctx->caretc);
    else
        XftColorAllocName(dpy, vis, cmap, "#1f1c1b", &ctx->caretc);

    // gutter
    if (ctx->dark_mode)
        XftColorAllocName(dpy, vis, cmap, "#414141", &ctx->gutterc);
    else
        XftColorAllocName(dpy, vis, cmap, "#bebebe", &ctx->gutterc);

}

static void free_palette(Display *dpy, CtvCtx *ctx)
{
    if (!ctx || ctx->palette.empty())
        return;

    for (size_t i=0; i<ctx->palette.size(); i++)
        XftColorFree(dpy, ctx->visual, ctx->cmap, &ctx->palette[i]);
    ctx->palette.clear();

    XftColorFree(dpy, ctx->visual, ctx->cmap, &ctx->selc);   // selection
    XftColorFree(dpy, ctx->visual, ctx->cmap, &ctx->caretc);    // caret
    XftColorFree(dpy, ctx->visual, ctx->cmap, &ctx->gutterc);    // gutter
}

static XftFont* open_font(Display *dpy, const char *family, double pt, int style)
{
    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return NULL;

    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)(family ? family : "monospace"));
    FcPatternAddDouble(pat, FC_SIZE, pt);
    FcPatternAddInteger(pat, FC_WEIGHT, (style & XMH_STYLE_BOLD) ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,  (style & XMH_STYLE_ITALIC) ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddInteger(pat, FC_SPACING, FC_MONO);  // here if needed

    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    XftFont *xf = match ? XftFontOpenPattern(dpy, match) : NULL;
    if (!xf && match) FcPatternDestroy(match); // avoid leak on failure
    FcPatternDestroy(pat);
    return xf;
}

static XftFont* open_font_like(Display *dpy, XftFont *base, int style)
{
    if (!base)
        return nullptr;

    FcPattern *pat = FcPatternDuplicate(base->pattern);
    if (!pat)
        return nullptr;

    // Preserve size
    double size;
    if (FcPatternGetDouble(base->pattern, FC_SIZE, 0, &size) == FcResultMatch)
    {
        FcPatternDel(pat, FC_SIZE);
        FcPatternAddDouble(pat, FC_SIZE, size);
    }

    // Preserve spacing (mono)
    int spacing;
    if (FcPatternGetInteger(base->pattern, FC_SPACING, 0, &spacing) == FcResultMatch)
    {
        FcPatternDel(pat, FC_SPACING);
        FcPatternAddInteger(pat, FC_SPACING, spacing);
    }

    // Set desired weight/slant
    FcPatternDel(pat, FC_WEIGHT);
    FcPatternDel(pat, FC_SLANT);
    FcPatternAddInteger(pat, FC_WEIGHT,
                        (style & XMH_STYLE_BOLD) ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        (style & XMH_STYLE_ITALIC) ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

    // Re-run match to get the proper face in the same family
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    FcPatternDestroy(pat);
    if (!match)
        return nullptr;

    XftFont *xf = XftFontOpenPattern(dpy, match);
    if (!xf)
        FcPatternDestroy(match);
    return xf;
}

static void ensure_font(CtvCtx *ctx)
{
    if (ctx->font)
        return;

    Display *dpy = XtDisplayOfObject(ctx->textWidget);
    if (!dpy)
        return;

    ctx->font = open_font(dpy, ctx->font_family ? ctx->font_family : "monospace",
                              (ctx->font_pt > 0 ? ctx->font_pt : 11.0), XMH_STYLE_NONE);
    if (!ctx->font)
        ctx->font = XftFontOpenName(dpy, DefaultScreen(dpy), "monospace-11");

    if (ctx->font)
    {
        // Update metrics
        update_metrics_from_font(ctx);

        // Record resolved family so styled faces use the exact same family
        FcChar8 *fam = NULL;
        if (FcPatternGetString(ctx->font->pattern, FC_FAMILY, 0, &fam) == FcResultMatch && fam)
        {
            free(ctx->font_family);
            ctx->font_family = strdup((const char*)fam);
        }
        // Optional: record resolved point size (keeps size stable if alias changed it)
        double pt = 0.0;
        if (FcPatternGetDouble(ctx->font->pattern, FC_SIZE, 0, &pt) == FcResultMatch && pt > 0.0)
            ctx->font_pt = pt;
    }
    else
    {
        ctx->ascent = 12; ctx->descent = 4; ctx->line_height = 16;
    }
}

static XftFont* get_font(CtvCtx *ctx, int style)
{
    ensure_font(ctx);
    Display *dpy = XtDisplayOfObject(ctx->textWidget);
    if (!dpy)
        return ctx->font;

    if ((style & XMH_STYLE_BOLD) && (style & XMH_STYLE_ITALIC))
    {
        if (!ctx->font_bolditalic)
            ctx->font_bolditalic = open_font_like(dpy, ctx->font, XMH_STYLE_BOLD|XMH_STYLE_ITALIC);
        return ctx->font_bolditalic ? ctx->font_bolditalic : ctx->font;
    }
    else if (style & XMH_STYLE_BOLD)
    {
        if (!ctx->font_bold)
            ctx->font_bold = open_font_like(dpy, ctx->font, XMH_STYLE_BOLD);
        return ctx->font_bold ? ctx->font_bold : ctx->font;
    }
    else if (style & XMH_STYLE_ITALIC)
    {
        if (!ctx->font_italic)
            ctx->font_italic = open_font_like(dpy, ctx->font, XMH_STYLE_ITALIC);

        return ctx->font_italic ? ctx->font_italic : ctx->font;
    }
    return ctx->font;
}

static XftFont *open_font_for_cp(Display *dpy, uint32_t cp, double pt)
{
    // Create a charset that contains just this codepoint
    FcCharSet *cs = FcCharSetCreate();
    FcCharSetAddChar(cs, cp);

    FcPattern *pat = FcPatternCreate();
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    if (pt > 0.0)
        FcPatternAddDouble(pat, FC_SIZE, pt);

    // IMPORTANT: no FC_SPACING here; allow proportional fonts
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);

    FcCharSetDestroy(cs);
    FcPatternDestroy(pat);

    if (!match)
        return nullptr;

    XftFont *xf = XftFontOpenPattern(dpy, match);
    if (!xf)
        FcPatternDestroy(match); // Xft takes ownership on success

    return xf;
}

static bool is_latin_codepoint(uint32_t cp)
{
    // Basic Latin
    if (cp <= 0x007F)
        return true;

    // Latin-1 Supplement (includes ü, é, etc.)
    if (cp >= 0x00A0 && cp <= 0x00FF)
        return true;

    // Latin Extended-A and B
    if (cp >= 0x0100 && cp <= 0x024F)
        return true;

    // Latin Extended Additional (accented letters)
    if (cp >= 0x1E00 && cp <= 0x1EFF)
        return true;

    return false;
}

static XftFont *get_font_for_codepoint(CtvCtx *ctx, const XmhColorToken *tok, uint32_t cp)
{
    Display *dpy = XtDisplayOfObject(ctx->textWidget);
    if (!dpy)
        return ctx->font;

    int style = tok ? tok->style : XMH_STYLE_NONE;

    // Base regular font (monospace), used for ASCII and as last resort
    XftFont *base_reg = get_font(ctx, XMH_STYLE_NONE);

    bool isLatin = is_latin_codepoint(cp);

    // 1) Latin: try styled font first (bold/italic)
    if (isLatin)
    {
        XftFont *styled = get_font(ctx, style);
        if (XftCharIndex(dpy, styled, cp))
            return styled;
        // fall through to base_reg if styled font doesn’t have this glyph
    }

    // 2) Then base regular
    if (XftCharIndex(dpy, base_reg, cp))
        return base_reg;

    // 3) Search all known fallbacks
    for (size_t i = 0; i < ctx->font_fallbacks.size(); ++i)
    {
        XftFont *fb = ctx->font_fallbacks[i];
        if (fb!=nullptr && XftCharIndex(dpy, fb, cp))
            return fb;
    }

    // 4) Need a new fallback: ask fontconfig for a face that has this codepoint.
    XftFont *fallback = open_font_for_cp(dpy, cp, ctx->font_pt);
    if (!fallback)
        return base_reg;   // last resort: missing-glyph box from base font

    ctx->font_fallbacks.push_back(fallback);

    return fallback;
}

/*!
 * \internal
 * \brief Ensure back‑buffer Pixmap and XftDraw are created and sized to the widget.
 */
static void ensure_backbuffer(CtvCtx *ctx)
{
    if (!XtIsRealized(ctx->textWidget))
        return;

    Display *dpy = XtDisplay(ctx->textWidget);
    Window   win = XtWindow(ctx->textWidget);

    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);

    ctx->visual = wa.visual;
    ctx->cmap   = wa.colormap;

    int w = wa.width;
    int h = wa.height;

    // (Re)create back buffer if size changed or missing
    if (!ctx->back_pix || ctx->back_w != w || ctx->back_h != h)
    {
        if (ctx->xft)
        {
            XftDrawDestroy(ctx->xft);
            ctx->xft = nullptr;
        }

        if (ctx->back_pix)
        {
            XFreePixmap(dpy, ctx->back_pix);
            ctx->back_pix = 0;
        }

        ctx->back_pix = XCreatePixmap(dpy, win, w, h, wa.depth);
        ctx->back_w   = w;
        ctx->back_h   = h;

        ctx->xft = XftDrawCreate(dpy, ctx->back_pix, ctx->visual, ctx->cmap);
        alloc_default_palette(dpy, ctx->visual, ctx->cmap, ctx);
    }
    else if (!ctx->xft)
    {
        // Pixmap exists but XftDraw was destroyed
        ctx->xft = XftDrawCreate(dpy, ctx->back_pix, ctx->visual, ctx->cmap);
    }
}

static void attach_clip_handlers(CtvCtx *ctx)
{
    if (!ctx || !ctx->scrolledWindow)
        return;

    Widget clip = NULL;
    XtVaGetValues(ctx->scrolledWindow, XmNclipWindow, &clip, NULL);
    if (clip)
    {
        // Make sure we receive wheel presses on the clip window
        XtAddEventHandler(clip, ButtonPressMask,    // Button4/5 are ButtonPress
                          False, buttonEH, (XtPointer)ctx);
    }
}


// UTF-8 continuation byte predicate
static inline bool is_cont(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

static Utf8Pos next_cp(const char *s, Utf8Pos len, Utf8Pos off)
{
    if (off >= len)
        return len;
    off++;
    while (off < len && is_cont((unsigned char)s[off]))
        off++;
    return off;
}

static Utf8Pos prev_cp(const char *s, Utf8Pos start, Utf8Pos off)
{
    if (off <= start)
        return start;
    off--;
    while (off > start && is_cont((unsigned char)s[off]))
        off--;
    return off;
}

static Utf8Pos align_cp_forward(const char *s, Utf8Pos len, Utf8Pos off)
{
    while (off < len && is_cont((unsigned char)s[off]))
        off++;
    return off;
}

static Utf8Pos align_cp_backward(const char *s, Utf8Pos start, Utf8Pos off)
{
    while (off > start && is_cont((unsigned char)s[off]))
        off--;
    return off;
}

static uint32_t decode_utf8(const char *s, Utf8Pos len, Utf8Pos off, Utf8Pos *nextOff)
{
    const unsigned char *p = (const unsigned char*)s + off;
    if (off >= len)
    {
        if (nextOff)
            *nextOff = len;
        return 0;
    }

    unsigned char c = *p;
    if (c < 0x80)
    {
        if (nextOff)
            *nextOff = off + 1;
        return c;
    }
    else if ((c & 0xE0) == 0xC0 && off + 1 < len)
    {
        if (nextOff)
            *nextOff = off + 2;
        return ((c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    else if ((c & 0xF0) == 0xE0 && off + 2 < len)
    {
        if (nextOff)
            *nextOff = off + 3;
        return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    else if ((c & 0xF8) == 0xF0 && off + 3 < len)
    {
        if (nextOff)
            *nextOff = off + 4;
        return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }

    // Invalid; treat as one byte
    if (nextOff)
        *nextOff = off + 1;
    return 0xFFFD;
}

static int cp_columns(uint32_t cp)
{
    wchar_t wc = (wchar_t)cp;
    int w = wcwidth(wc);
    if (w >= 0)
        return w;      // 0, 1 or 2

    // Fallback heuristic if wcwidth doesn’t know this char
    if (cp == 0)
        return 0;

    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
        return 0;

    // Common combining mark ranges (not exhaustive, but much better)
    if ((cp >= 0x0300 && cp <= 0x036F) ||  // Combining Diacritical Marks
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||  // Combining Diacritical Marks Extended
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||  // Combining Diacritical Marks Supplement
        (cp >= 0x20D0 && cp <= 0x20FF) ||  // Combining Diacritical Marks for Symbols
        (cp >= 0xFE20 && cp <= 0xFE2F))    // Combining Half Marks
    return 0;

    // Zero-width joiner / non-joiner
    if (cp == 0x200D || cp == 0x200C)
        return 0;

    // Variation selectors (text/emoji variants)
    if ((cp >= 0xFE00 && cp <= 0xFE0F) ||      // VS1..VS16
        (cp >= 0xE0100 && cp <= 0xE01EF))      // additional VS
    return 0;

    // Emoji skin tone modifiers: U+1F3FB..U+1F3FF
    if (cp >= 0x1F3FB && cp <= 0x1F3FF)
        return 0;

    if (cp < 0x80)
        return 1; // printable ASCII

    // CJK Unified Ideographs & Hangul & Fullwidth forms → 2 columns
    if ((cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
        (cp >= 0x2E80 && cp <= 0xA4CF) ||   // CJK, Yi, etc.
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul Syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK Compatibility Ideographs
        (cp >= 0xFF01 && cp <= 0xFF60) ||   // Fullwidth ASCII variants
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) || // many emoji + pictographs
        (cp >= 0x1F900 && cp <= 0x1F9FF))
        return 2;

    return 1;
}

static int line_index_from_pos(CtvCtx *ctx, Utf8Pos p)
{
    if (ctx->line_starts.empty())
        return 0;

    std::vector<Utf8Pos> &ls = ctx->line_starts;
    auto it = std::upper_bound(ls.begin(), ls.end(), p);
    if (it == ls.begin())
        return 0;  // p is before or at the first line start

    // step back to last element <= p
    return static_cast<int>(std::distance(ls.begin(), it - 1));
}

// Compute gutter width (pixels) based on number of digits and a padding
static int gutter_width(CtvCtx *ctx)
{
    if (ctx->gutter_enabled==false)
        return 0;

    // digits = floor(log10(line_count)) + 1; minimal 2 digits
    int lines = std::max(1, ctx->line_count);
    int digits = 1;
    while (lines >= 10)
    {
        lines /= 10;
        digits++;
    }
    digits = std::max(digits, 2);

    int cw = ctx->cellwidth;
    // padding: one cell before and after
    return (digits * cw) + (2 * cw);
}

/*!
 * \internal
 * \brief Recompute line starts and pixel width, update virtual size of the widget.
 */
static void compute_lines(CtvCtx *ctx)
{
    ensure_font(ctx);

    const char *s = ctx->text ? ctx->text : "";
    ctx->line_starts.clear();
    ctx->line_starts.push_back(0);
    for (Utf8Pos i = 0; i < ctx->text_len; ++i)
        if (s[i] == '\n')
            ctx->line_starts.push_back(i + 1);

    ctx->line_count = ctx->line_starts.size();
    ctx->max_line_px = 0;

    int cw = ctx->cellwidth;

    for (int li = 0; li < ctx->line_count; ++li)
    {
        Utf8Pos ls = ctx->line_starts[li];
        Utf8Pos le = line_end_no_nl(ctx, li);
        int col = 0;
        Utf8Pos off = ls;
        while (off < le)
        {
            Utf8Pos nextOff;
            uint32_t cp = decode_utf8(ctx->text, ctx->text_len, off, &nextOff);
            col += cp_columns(cp);
            off = nextOff;
        }
        int wpx = col * cw;
        ctx->max_line_px = std::max(ctx->max_line_px, wpx);
    }

    ctx->gutter_px = gutter_width(ctx);
    int lh = (ctx->line_height > 0) ? ctx->line_height : 16;

    ctx->width_px  = ctx->max_line_px + ctx->gutter_px;
    ctx->height_px = std::max(1, ctx->line_count) * lh;

    update_scrollbars(ctx);
}

/*! \internal Return end position of a line without the trailing newline. */
static Utf8Pos line_end_no_nl(CtvCtx *ctx, int li)
{
    Utf8Pos s = ctx->line_starts[li];
    Utf8Pos e = (li+1<int(ctx->line_starts.size())) ? ctx->line_starts[li+1] : ctx->text_len;
    if (e>s && ctx->text[e-1]=='\n')
        e--;

    return e;
}

/*!
 * \internal
 * \brief Free content-related allocations.
 */
static void free_text(CtvCtx *ctx)
{
    if (!ctx)
        return;
    if (ctx->text)
    {
        free(ctx->text);
        ctx->text=nullptr;
    }
    ctx->text_len = 0;

    ctx->tokens.clear();

    ctx->line_count = 0;
    ctx->max_line_px =0;
}

static size_t first_token_for_pos(CtvCtx *ctx, Utf8Pos ls)
{
    auto &tokens = ctx->tokens;
    if (tokens.empty())
        return 0;

    auto it = std::lower_bound(tokens.begin(), tokens.end(), ls,
                               [](const XmhColorToken &t, Utf8Pos value) {
                                   return (t.start + t.len) <= value; // first token whose end > ls
                               });

    return (size_t)std::distance(tokens.begin(), it);
}

/*!
 * \internal
 * \brief Convert widget x,y to nearest text byte position.
 */
static Utf8Pos xy_to_pos(CtvCtx *ctx, int x, int y, CtvCoordMode mode)
{
    if (!ctx || !ctx->text || ctx->line_count == 0)
        return 0;

    int hScroll = 0;
    int topLine = 0;
    if (mode == CTV_COORD_VIEWPORT)
    {
        if (ctx->hbar && XtIsManaged(ctx->hbar))
            XtVaGetValues(ctx->hbar, XmNvalue, &hScroll, NULL);
        int visible_lines = get_visible_lines(ctx);
        if (ctx->vbar)
            topLine = topLine_from_scroll(ctx, ctx->line_count, visible_lines);
        x += hScroll;
        y += topLine * ctx->line_height;
    }

    int lh = ctx->line_height > 0 ? ctx->line_height : 16;
    int li = y / lh;
    li = std::max(0, std::min(li, ctx->line_count - 1));

    Utf8Pos ls = ctx->line_starts[li];
    Utf8Pos le = line_end_no_nl(ctx, li);

    int cw = ctx->cellwidth;

    int colTarget = 0;
    int xContent = x - ctx->gutter_px;
    if (xContent > 0)
        colTarget = xContent / cw;

    int col = 0;
    Utf8Pos off = ls;
    while (off < le)
    {
        Utf8Pos nextOff;
        uint32_t cp = decode_utf8(ctx->text, ctx->text_len, off, &nextOff);
        int w = cp_columns(cp);
        if (col + w > colTarget)
            return off;
        col += w;
        off = nextOff;
    }
    return le;
}

/*!
 * \internal
 * \brief Convert byte position to x,y in widget coordinates.
 * \return 1 if the position is valid, 0 otherwise.
 */
static int pos_to_xy(CtvCtx *ctx, Utf8Pos p, int *x, int *y, CtvCoordMode mode)
{
    if (!ctx->text || ctx->line_count == 0)
    {
        if (x) *x = 0;
        if (y) *y = 0;
        return 0;
    }

    ensure_font(ctx);
    int cw = ctx->cellwidth;

    p = std::max(0, std::min(ctx->text_len, p));
    p = align_cp_backward(ctx->text, 0, p);
    int li = line_index_from_pos(ctx,  p);
    Utf8Pos ls = ctx->line_starts[li];
    Utf8Pos le = line_end_no_nl(ctx, li);
    p = std::max(ls, std::min(le, p));

    // Count columns from line start to p
    int col = 0;
    Utf8Pos off = ls;
    while (off < p)
    {
        Utf8Pos nextOff;
        uint32_t cp = decode_utf8(ctx->text, ctx->text_len, off, &nextOff);
        col += cp_columns(cp);
        off = nextOff;
    }

    int xi = ctx->gutter_px + col * cw;
    int yi = li * ctx->line_height;

    if (mode == CTV_COORD_VIEWPORT)
    {
        int h = 0;
        if (ctx->hbar && XtIsManaged(ctx->hbar))
            XtVaGetValues(ctx->hbar, XmNvalue, &h, NULL);
        int visible_lines = get_visible_lines(ctx);
        int topLine = 0;
        if (ctx->vbar)
            topLine = topLine_from_scroll(ctx, ctx->line_count, visible_lines);
        xi -= h;
        yi -= topLine * ctx->line_height;
    }

    if (x)
        *x = xi;
    if (y)
        *y = yi;
    return 1;
}

/*! \internal Clear a rectangle using the widget background color. */
static void clear_rect(CtvCtx *ctx, int x, int y, int w, int h)
{
    if (!XtIsRealized(ctx->textWidget) || !ctx->back_pix)
        return;

    Display *dpy = XtDisplay(ctx->textWidget);
    if (!ctx->bg_gc)
        ctx->bg_gc = XCreateGC(dpy, ctx->back_pix, 0, NULL);

    XSetForeground(dpy, ctx->bg_gc, ctx->bg);
    XFillRectangle(dpy, ctx->back_pix, ctx->bg_gc,
                   x, y, (unsigned)w, (unsigned)h);
}

/*!
 * \internal
 * \brief Expose handler: draws visible lines, selection, and caret.
 */
static void draw_expose(CtvCtx *ctx, XExposeEvent *ex)
{
    if (!XtIsRealized(ctx->textWidget))
        return;

    ensure_font(ctx);
    ensure_backbuffer(ctx);
    if (!ctx->xft || !ctx->font)
        return;

    if (ctx->text_len == 0)
    {
        // The pixmap may still contain the previous source display.
        // Clear the complete back buffer before copying it to the window.
        clear_rect(ctx, 0, 0, ctx->back_w, ctx->back_h);

        Display *dpy = XtDisplay(ctx->textWidget);
        Window win = XtWindow(ctx->textWidget);

        XCopyArea(dpy, ctx->back_pix, win, ctx->bg_gc,
                0, 0, ctx->back_w, ctx->back_h,
                0, 0);
        return;
    }

    int curH = 0;
    int sliderH = 1;
    if (ctx->hbar && XtIsManaged(ctx->hbar))
        XtVaGetValues(ctx->hbar, XmNvalue, &curH, XmNsliderSize, &sliderH, NULL);

    int visible_lines = get_visible_lines(ctx);
    int topLine = 0;
    if (ctx->vbar)
        topLine = topLine_from_scroll(ctx, ctx->line_count, visible_lines);

    bool viewportChanged = (curH != ctx->prev_h) || (topLine != ctx->prev_v);
    XRectangle r = { (short)ex->x, (short)ex->y, (unsigned short)ex->width, (unsigned short)ex->height };
    XftDrawSetClipRectangles(ctx->xft, 0, 0, &r, 1);
    clear_rect(ctx, ex->x, ex->y, ex->width, ex->height);

    int lh = ctx->line_height > 0 ? ctx->line_height : 16;
    int first = std::max(0, topLine + ex->y / lh);
    int last  = std::min(ctx->line_count - 1, topLine + (ex->y + ex->height) / lh);

    Display *dpy = XtDisplay(ctx->textWidget);
    int cw = ctx->cellwidth;
    XftColor *textNumColor = &ctx->palette[0];
    int gutter = ctx->gutter_px;
    int content_offset = -curH;           // entire content (gutter + text) offset
    for (int li=first; li<=last; ++li)
    {
        int line_y  = (li - topLine) * lh;
        int baseline = line_y + ctx->ascent;
        int ytop     = line_y;

        if (gutter > 0)
        {
            int gutter_left  = content_offset;
            int gutter_width = std::max(0, gutter - 1);

            XftDrawRect(ctx->xft, &ctx->gutterc,
                        gutter_left, ytop,
                        gutter_width, lh);
            XftDrawRect(ctx->xft, textNumColor,
                        gutter_left + gutter - 1, ytop,
                        1, lh);

            char numbuf[16];
            snprintf(numbuf, sizeof(numbuf), "%d", li + 1);
            int nx = gutter_left + gutter - cw * strlen(numbuf) - cw;
            XftDrawStringUtf8(ctx->xft, textNumColor, ctx->font,
                              nx, baseline, (FcChar8*)numbuf, (int)strlen(numbuf));
        }

        Utf8Pos ls = ctx->line_starts[li];
        Utf8Pos le = line_end_no_nl(ctx, li);

        // Selection overlay
        if (ctx->has_sel && ctx->sel_end>ctx->sel_start)
        {
            Utf8Pos s = std::max(ls, ctx->sel_start);
            Utf8Pos e = std::min(le, ctx->sel_end);
            if (e > s)
            {
                int xs, ys, xe, ye;
                pos_to_xy(ctx, s, &xs, &ys, CTV_COORD_ABSOLUTE);
                pos_to_xy(ctx, e, &xe, &ye, CTV_COORD_ABSOLUTE);
                xs -= curH;
                xe -= curH;
                ys -= topLine * lh;
                int wpx = xe - xs;
                XftDrawRect(ctx->xft, &ctx->selc, xs, line_y, wpx, lh);
            }
        }

        // text start x: right after gutter, with same content_offset
        Utf8Pos pos = ls;
        auto &tokens = ctx->tokens;
        size_t token_count = tokens.size();
        size_t ti = first_token_for_pos(ctx, ls);
        int x = content_offset + gutter;

        while (pos < le)
        {
            const XmhColorToken *tok = nullptr;
            int color_idx = 0;
            while (ti < token_count)
            {
                Utf8Pos ts = tokens[ti].start;
                Utf8Pos te = ts + tokens[ti].len;
                if (te > pos)
                {
                    if (ts >= le)
                        ti = token_count;
                    break;
                }
                ++ti;
            }
            if (ti < token_count)
            {
                Utf8Pos ts = tokens[ti].start;
                Utf8Pos te = ts + tokens[ti].len;
                if (ts <= pos && pos < te)
                {
                    tok = &tokens[ti];
                    color_idx = tok->color;
                }
            }

            XftColor *col = &ctx->palette[0];
            if (color_idx >= 0 && color_idx < (int)ctx->palette.size())
                col = &ctx->palette[color_idx];

            // Decode single codepoint
            Utf8Pos next;
            uint32_t cp = decode_utf8(ctx->text, ctx->text_len, pos, &next);
            int glyphLen = (int)(next - pos);
            if (glyphLen <= 0)
                break;

            XftFont *font = get_font_for_codepoint(ctx, tok, cp);

            // Compute logical width in columns and pixels
            int cols = cp_columns(cp);
            if (cols <=0)
            {
                pos = next;
                continue;
            }
            int wpx = cols * cw;

            // Optionally skip drawing if completely left of clip
            if (x + wpx >= ex->x && x <= ex->x + ex->width)
                XftDrawStringUtf8(ctx->xft, col, font, x, baseline,
                                (const FcChar8*)(ctx->text + pos), glyphLen);

            x += wpx;
            pos = next;
        }
    }

    // Caret in widget coordinates, adjusted for scroll
    if (ctx->caret_visible)
    {
        int cx, cy;
        if (pos_to_xy(ctx, ctx->caret, &cx, &cy, CTV_COORD_ABSOLUTE))
        {
            cx -= curH;
            cy -= topLine * lh;
            XftDrawRect(ctx->xft, &ctx->caretc, cx, cy + 2,
                        2, ctx->line_height - 4);
        }
    }

    XftDrawSetClip(ctx->xft, NULL);

    if (viewportChanged)
    {
        CtvTextRec *tw = (CtvTextRec*)ctx->textWidget;
        if (tw && tw->ctvtext.viewport_changed_callback)
            XtCallCallbackList(ctx->textWidget, tw->ctvtext.viewport_changed_callback, nullptr);

        ctx->prev_h = curH;
        ctx->prev_v = topLine;
    }

    dpy = XtDisplay(ctx->textWidget);
    Window   win = XtWindow(ctx->textWidget);
    if (ctx->back_pix)
    {
        if (!ctx->bg_gc)
            ctx->bg_gc = XCreateGC(dpy, win, 0, NULL);

        XCopyArea(dpy, ctx->back_pix, win, ctx->bg_gc,
                  0, 0, ctx->back_w, ctx->back_h,
                  0, 0);
    }
}

static void update_selection(CtvCtx *ctx, Utf8Pos newpos, bool extend)
{
    if (!extend)
    {
        ctx->caret = newpos;
        ctx->has_sel = 0;
        ctx->sel_start = ctx->sel_end = ctx->caret;
        ctx->sel_anchor = ctx->caret;
        return;
    }
    // start selection if not active
    if (!ctx->has_sel)
        ctx->sel_anchor = ctx->caret;
    ctx->caret = newpos;
    if (newpos < ctx->sel_anchor)
    {
        ctx->sel_start = newpos;
        ctx->sel_end = ctx->sel_anchor;
    }
    else
    {
        ctx->sel_start = ctx->sel_anchor;
        ctx->sel_end = newpos;
    }
    ctx->has_sel = (ctx->sel_end > ctx->sel_start);
}

static void move_h(CtvCtx *ctx, int dir, bool extend)
{
    if (!ctx || !ctx->text)
        return;
    Utf8Pos p = ctx->caret;
    if (dir < 0)
        p = prev_cp(ctx->text, 0, p);
    else
        p = next_cp(ctx->text, ctx->text_len, p);
    update_selection(ctx, p, extend);
    ctx->goal_x = -1; // horizontal move resets goal column
    XmhColorTextViewShowPosition(ctx->textWidget, ctx->caret);
    queue_redraw(ctx);
}

static void move_v(CtvCtx *ctx, int dlines, bool extend)
{
    if (!ctx || !ctx->text || ctx->line_height <= 0) return;

    int cx=0, cy=0;
    pos_to_xy(ctx, ctx->caret, &cx, &cy, CTV_COORD_ABSOLUTE);

    if (ctx->goal_x < 0)
        ctx->goal_x = cx;

    int new_y = cy + dlines * ctx->line_height;
    int max_y = (ctx->line_count - 1) * ctx->line_height;
    new_y = std::max(0, std::min(new_y, max_y));

    Utf8Pos newpos = xy_to_pos(ctx, ctx->goal_x, new_y, CTV_COORD_ABSOLUTE);
    update_selection(ctx, newpos, extend);

    XmhColorTextViewShowPosition(ctx->textWidget, ctx->caret);
    queue_redraw(ctx);
}

static void move_home_end(CtvCtx *ctx, bool to_end, bool ctrl, bool extend)
{
    if (!ctx || !ctx->text)
        return;
    Utf8Pos p = ctx->caret;
    Utf8Pos target = 0;
    if (ctrl)
    {
        target = to_end ? ctx->text_len : 0;
    }
    else
    {
        int li = line_index_from_pos(ctx, p);
        Utf8Pos ls = ctx->line_starts[li];
        Utf8Pos le = line_end_no_nl(ctx, li);
        target = to_end ? le : ls;
    }
    update_selection(ctx, target, extend);
    ctx->goal_x = -1;
    XmhColorTextViewShowPosition(ctx->textWidget, ctx->caret);
    queue_redraw(ctx);
}

static Time event_time(XEvent *event, Display *dpy)
{
    if (!event)
        return XtLastTimestampProcessed(dpy);

    switch (event->type)
    {
        case ButtonPress:
        case ButtonRelease:
            return event->xbutton.time;
        case KeyPress:
        case KeyRelease:
            return event->xkey.time;
        case MotionNotify:
            return event->xmotion.time;
        default:
            return XtLastTimestampProcessed(dpy);
    }
}

static Boolean copy_selection_to_clipboard(Widget w, CtvCtx *ctx, Time time)
{
    if (!ctx || !ctx->has_sel || ctx->sel_end <= ctx->sel_start)
        return False;  // nothing to copy

    if (!XtIsRealized(w))
        return False;

    Display *dpy = XtDisplay(w);
    Window   win = XtWindow(w);
    if (!dpy || !win)
        return False;

    Utf8Pos start = ctx->sel_start;
    Utf8Pos end   = ctx->sel_end;
    long    len   = (long)(end - start);
    if (len <= 0)
        return False;

    char *buf = (char*)malloc((size_t)len);
    if (!buf)
        return False;
    memcpy(buf, ctx->text + start, (size_t)len);

    if (time == CurrentTime || time == 0)
        time = XtLastTimestampProcessed(dpy);

    XmString label = XmStringCreateLocalized((char*)"XmhColorTextView");
    long item_id   = 0;

    int status = XmClipboardStartCopy(dpy, win,
                                      label,        /* XmString */
                                      time,
                                      w,
                                      NULL,
                                      &item_id);
    XmStringFree(label);

    if (status != XmClipboardSuccess) {
        free(buf);
        return False;
    }

    long data_id = 0;
    status = XmClipboardCopy(dpy, win,
                             item_id,
                             (char*)"UTF8_STRING",   // or "STRING"
                             buf, len,
                             0, &data_id);
    free(buf);

    if (status != XmClipboardSuccess)
    {
        XmClipboardCancelCopy(dpy, win, item_id);
        return False;
    }

    XmClipboardEndCopy(dpy, win, item_id);
    return True;
}

/*! \internal Queue an immediate redraw of the full widget (used after state changes). */
static void queue_redraw(CtvCtx *ctx)
{
    if (!ctx || !XtIsRealized(ctx->textWidget))
        return;

    Display  *dpy = XtDisplay(ctx->textWidget);
    Window    win = XtWindow(ctx->textWidget);
    Dimension w   = 0, h = 0;
    XtVaGetValues(ctx->textWidget, XmNwidth, &w, XmNheight, &h, NULL);

    XExposeEvent ex;
    memset(&ex, 0, sizeof(ex));
    ex.type    = Expose;
    ex.display = dpy;
    ex.window  = win;
    ex.x       = 0;
    ex.y       = 0;
    ex.width   = w;
    ex.height  = h;

    draw_expose(ctx, &ex);
}

static void scroll_v_by_lines(CtvCtx *ctx, int n)
{
    if (!ctx || !ctx->vbar)
        return;

    int visible_lines = get_visible_lines(ctx);
    int N = ctx->line_count;
    int R = std::max(0, N - visible_lines);

    int curTop = topLine_from_scroll(ctx, N, visible_lines);
    int newTop = std::max(0, std::min(curTop + n, R));

    scroll_from_topLine(ctx, newTop, N, visible_lines);

    // We updated the scrollbar programmatically (notify=False),
    // so we must request a redraw ourselves.
    queue_redraw(ctx);
}

// Scroll horizontally by dx pixels (positive = right, negative = left)
static void scroll_h_by_px(CtvCtx *ctx, int dx)
{
    if (!ctx || !ctx->hbar)
        return;
    if (!XtIsManaged(ctx->hbar))
        return;   // no horizontal bar: treat as no horizontal scroll
    if (ctx->width_px <= ctx->viewport_width)
        return;   // no horizontal overflow -> no scrolling
    int h = 0;
    XtVaGetValues(ctx->hbar, XmNvalue, &h, NULL);
    set_hscroll(ctx, h + dx);
}

static void adjust_font_size(CtvCtx *ctx, double delta)
{
    if (!ctx || !ctx->textWidget)
        return;

    ensure_font(ctx);

    double current = ctx->font_pt > 0.0 ? ctx->font_pt : 11.0;
    double target = std::clamp(current + delta, 6.0, 72.0);

    target = std::round(target * 10.0) / 10.0;

    if (std::fabs(target - current) < 0.05)
        return;

    const char *family = (ctx->font_family && *ctx->font_family) ? ctx->font_family : "monospace";
    XmhColorTextViewSetFont(ctx->textWidget, family, target);
}


/*! \internal Mouse handling for selection and caret. */
static void buttonEH(Widget, XtPointer client, XEvent *ev, Boolean *cont)
{
    CtvCtx *ctx = (CtvCtx*)client;

    // Mouse wheel: Button4 (up), Button5 (down)
    if (ev->type == ButtonPress && (ev->xbutton.button == Button4 || ev->xbutton.button == Button5))
    {
        int dir = (ev->xbutton.button == Button4) ? -1 : +1;
        unsigned int state = ev->xbutton.state;
        bool shift = (state & ShiftMask);
        bool ctrl  = (state & ControlMask);

        // Default: 3 lines per notch; Ctrl accelerates to 10 lines; Shift scrolls horizontally
        int lines = ctrl ? 10 : 3;
        if (!shift && !ctrl)
            scroll_v_by_lines(ctx, dir * lines); // Vertical scroll; caret moves only if it leaves viewport
        else if (shift)
            scroll_h_by_px(ctx, dir * lines * (ctx->line_height > 0 ? ctx->line_height : 40)); // Horizontal scroll step
        else
            adjust_font_size(ctx, 1.0 * dir);

        if (cont)
            *cont = False; // stop further processing
        return;
    }

    // Existing selection/caret handling
    if (ev->type == ButtonPress && ev->xbutton.button == Button1)
    {
        int x = ev->xbutton.x, y = ev->xbutton.y;
        int h = 0, hslider = 0;
        if (ctx->hbar && XtIsManaged(ctx->hbar))
            XtVaGetValues(ctx->hbar, XmNvalue, &h, XmNsliderSize, &hslider, NULL);
        ctx->drag_anchor = xy_to_pos(ctx, x, y, CTV_COORD_VIEWPORT);
        ctx->caret = ctx->drag_anchor;
        ctx->sel_start = ctx->sel_end = ctx->drag_anchor;
        ctx->sel_anchor = ctx->drag_anchor;
        ctx->has_sel = 0;
        ctx->dragging = 1;
        ctx->goal_x = -1;          // reset column goal
        queue_redraw(ctx);
    }
    else if (ev->type == MotionNotify && ctx->dragging)
    {
        int x = ev->xmotion.x, y = ev->xmotion.y;
        Utf8Pos p = xy_to_pos(ctx, x, y, CTV_COORD_VIEWPORT);
        if (p < ctx->drag_anchor)
        {
            ctx->sel_start = p;
            ctx->sel_end = ctx->drag_anchor;
        }
        else
        {
            ctx->sel_start = ctx->drag_anchor;
            ctx->sel_end = p;
        }
        ctx->has_sel = (ctx->sel_end > ctx->sel_start);
        ctx->caret = p;
        queue_redraw(ctx);
    }
    else if (ev->type == ButtonRelease && ev->xbutton.button == Button1)
    {
        ctx->dragging = 0;
    }
}

static void keyEH(Widget, XtPointer client, XEvent *ev, Boolean *cont)
{
    if (ev->type != KeyPress)
        return;
    CtvCtx *ctx = (CtvCtx*)client;
    if (!ctx)
        return;

    KeySym ks = XLookupKeysym(&ev->xkey, 0);
    unsigned int st = ev->xkey.state;
    bool shift = (st & ShiftMask);
    bool ctrl  = (st & ControlMask);

    switch (ks)
    {
        case XK_Left:
        case XK_KP_Left:
            move_h(ctx, -1, shift);
            break;
        case XK_Right:
        case XK_KP_Right:
            move_h(ctx, +1, shift);
            break;
        case XK_Up:
        case XK_KP_Up:
            move_v(ctx, -1, shift);
            break;
        case XK_Down:
        case XK_KP_Down:
            move_v(ctx, +1, shift);
            break;
        case XK_Home:
        case XK_KP_Home:
            move_home_end(ctx, false, ctrl, shift);
            break;
        case XK_End:
        case XK_KP_End:
            move_home_end(ctx, true,  ctrl, shift);
            break;
        case XK_Prior:
        case XK_KP_Prior:
            move_v(ctx, -get_visible_lines(ctx), shift);
            break;
        case XK_Next:
            move_v(ctx, +get_visible_lines(ctx), shift);
            break;
        case XK_period:
        case XK_KP_Add:
            if (!ctrl)
                return;
            if ((ks==XK_period && shift) || ks==XK_KP_Add)
                adjust_font_size(ctx, +1.0);
            break;
        case XK_comma:
        case XK_KP_Subtract:
            if (!ctrl)
                return;
            if ((ks==XK_comma && shift) || ks==XK_KP_Subtract)
                adjust_font_size(ctx, -1.0);
            break;
        case XK_greater:
        case XK_less:
            if (!ctrl)
                return;
            if (shift)
                adjust_font_size(ctx, -1.0);
            else
                adjust_font_size(ctx, +1.0);
            break;
        default:
            return; // let others handle
    }
    if (cont)
        *cont = False; // consume
}

/*!
 * \brief Create the scrolled, colored text viewer (XmhColorTextView widget inside XmScrolledWindow).
 *
 * Creates an XmScrolledWindow and places the XmhColorTextView widget inside it for rendering.
 * The XmhColorTextView widget is returned; use SVWTextGetScrolledWindow() to access the scrolled window.
 *
 * \param parent Parent widget.
 * \param name   Name for the scrolled window.
 * \param args   Optional args for the XmhColorTextView widget.
 * \param n      Number of args.
 * \return The XmhColorTextView widget used for rendering.
 */
Widget CreateXmhColorTextView(Widget parent, const char *name,
                              Arg *args, Cardinal n)
{
    // Create scrolled window
    char swname[256];
    snprintf(swname, sizeof(swname), "%sSW", (name && *name) ? name : "XmhColorTextView");

    Arg sargs[8];
    int k = 0;
    XtSetArg(sargs[k], XmNscrollingPolicy, XmAPPLICATION_DEFINED); k++;
    XtSetArg(sargs[k], XmNvisualPolicy, XmVARIABLE); k++;
    XtSetArg(sargs[k], XmNscrollBarDisplayPolicy, XmSTATIC); k++;
    Widget sw = XmCreateScrolledWindow(parent, swname, sargs, k);

    // Create scrollbars explicitly
    Widget vbar = XmCreateScrollBar(sw, (char*)"verticalScrollBar", NULL, 0);
    Widget hbar = XmCreateScrollBar(sw, (char*)"horizontalScrollBar", NULL, 0);

    XtVaSetValues(vbar, XmNorientation, XmVERTICAL,   NULL);
    XtVaSetValues(hbar, XmNorientation, XmHORIZONTAL, NULL);

    /// \todo this is a workaround for a very thick horizontal scrollbar --> find real cause
    // Match thickness: use vbar's preferred width as a reference
    Dimension sb_thickness = 0;
    XtVaGetValues(vbar, XmNwidth, &sb_thickness, NULL);
    if (sb_thickness == 0)
        sb_thickness = 16;  // fallback

    // Vertical bar: thin width; horizontal bar: same thin height
    XtVaSetValues(vbar, XmNwidth,  sb_thickness, NULL);
    XtVaSetValues(hbar, XmNheight, sb_thickness, NULL);

    XtManageChild(vbar);
    XtManageChild(hbar);

    // Create the text widget as the work window
    Widget w = XtCreateManagedWidget(name ? name : "XmhColorTextView",
                                     XmhColorTextViewWidgetClass,
                                     sw, args, n);

    // Tell the scrolled window which children are which
    XtVaSetValues(sw,
                  XmNworkWindow,         w,
                  XmNverticalScrollBar,  vbar,
                  XmNhorizontalScrollBar,hbar,
                  NULL);

    XtManageChild(sw);

    // Wire ctx and scrollbars
    CtvCtx *ctx = get_ctx(w);
    if (ctx)
    {
        ctx->scrolledWindow = sw;
        ctx->vbar = vbar;
        ctx->hbar = hbar;

        XtAddCallback(vbar, XmNvalueChangedCallback, vscrollCB, ctx);
        XtAddCallback(vbar, XmNdragCallback,         vscrollCB, ctx);
        XtAddCallback(hbar, XmNvalueChangedCallback, hscrollCB, ctx);
        XtAddCallback(hbar, XmNdragCallback,         hscrollCB, ctx);

        attach_clip_handlers(ctx);    // wheel on clip window
        update_scrollbars(ctx);       // initial ranges
    }

    return w;
}

//==========================================================
// Core-like lifecycle for the XmhColorTextView widget class
//==========================================================
static void Initialize(Widget req, Widget w, ArgList args, Cardinal *num)
{
    (void)req; (void)args; (void)num;
    CtvTextRec *tw = (CtvTextRec*)w;

    // Create ctx and attach to widget
    CtvCtx *ctx = new CtvCtx;
    ctx->textWidget = w;
    ctx->caret_visible = 1;
    ctx->font_family = strdup(tw->ctvtext.fontFamily ? tw->ctvtext.fontFamily : "monospace");
    ctx->font_pt     = (tw->ctvtext.fontSize > 0.0f ? tw->ctvtext.fontSize : 11.0f);
    XtVaGetValues(w, XmNbackground, &ctx->bg, NULL);
    tw->ctvtext.ctx = ctx;

    // Initial content
    const char *s = tw->ctvtext.string ? tw->ctvtext.string : "";
    XmhColorTextViewSetString(ctx->textWidget, s);

    // If already placed in an XmScrolledWindow, cache scrollbars
    Widget parent = XtParent(w);
    if (parent && XtIsSubclass(parent, xmScrolledWindowWidgetClass))
    {
        ctx->scrolledWindow = parent;
        XtVaGetValues(parent,
                      XmNverticalScrollBar,   &ctx->vbar,
                      XmNhorizontalScrollBar, &ctx->hbar,
                      NULL);

        if (ctx->vbar)
        {
            XtAddCallback(ctx->vbar, XmNvalueChangedCallback, vscrollCB, ctx);
            XtAddCallback(ctx->vbar, XmNdragCallback,         vscrollCB, ctx);
        }
        if (ctx->hbar)
        {
            XtAddCallback(ctx->hbar, XmNvalueChangedCallback, hscrollCB, ctx);
            XtAddCallback(ctx->hbar, XmNdragCallback,         hscrollCB, ctx);
        }
        attach_clip_handlers(ctx);
        update_scrollbars(ctx);
    }

    // Event handlers
    XtAddEventHandler(w, ButtonPressMask | ButtonMotionMask | ButtonReleaseMask,
                      False, buttonEH, ctx);
    XtAddEventHandler(w, KeyPressMask, False, keyEH, ctx);
    XtAddEventHandler(w, StructureNotifyMask, False, configureEH, ctx);

    XtVaSetValues(w, XmNtraversalOn, True, nullptr);

    // Set initial geometry from columns/rows
    ensure_font(ctx);                    // ensure font metrics
    int lh = (ctx->line_height > 0) ? ctx->line_height : 16;
    int cw = ctx->cellwidth;
    Dimension wpref = (tw->ctvtext.columns > 0) ? (Dimension)(tw->ctvtext.columns * cw) : 0;
    Dimension hpref = (tw->ctvtext.rows    > 0) ? (Dimension)(tw->ctvtext.rows    * lh) : 0;
    if (wpref && hpref)
        XtVaSetValues(w, XmNwidth, wpref, XmNheight, hpref, nullptr);
    else if (wpref)
        XtVaSetValues(w, XmNwidth, wpref, nullptr);
    else if (hpref)
        XtVaSetValues(w, XmNheight, hpref, nullptr);

    // Initialize viewport dimensions and scrollbars
    Dimension curW = 0, curH = 0;
    XtVaGetValues(w, XmNwidth, &curW, XmNheight, &curH, nullptr);
    ctx->viewport_width  = curW;
    ctx->viewport_height = curH;
    update_scrollbars(ctx);
}

static void Destroy(Widget w)
{
    CtvTextRec *tw = (CtvTextRec*)w;
    if (!tw->ctvtext.ctx)
        return;
    destroyCB(w, (XtPointer)tw->ctvtext.ctx, nullptr);
    tw->ctvtext.ctx = nullptr;
}

static Boolean SetValues(Widget old, Widget req, Widget nw, ArgList args, Cardinal *num)
{
    (void)req; (void)args; (void)num;
    CtvTextRec *ow = (CtvTextRec*)old;
    CtvTextRec *nw_ = (CtvTextRec*)nw;
    CtvCtx *ctx = nw_->ctvtext.ctx;

    Boolean do_recompute = False;
    Boolean do_redraw    = False;

    // Background change
    Pixel obg=0, nbg=0;
    XtVaGetValues(old, XmNbackground, &obg, NULL);
    XtVaGetValues(nw,  XmNbackground, &nbg, NULL);
    if (nbg != obg)
    {
        XmhColorTextViewSetBackgroundPixel(nw, nbg);
        do_redraw = True;
    }

    // Font changes
    const char *ofam = ow->ctvtext.fontFamily;
    const char *nfam = nw_->ctvtext.fontFamily;
    float opt = ow->ctvtext.fontSize;
    float npt = nw_->ctvtext.fontSize;
    if ((nfam && (!ofam || strcmp(nfam, ofam)!=0)) || (opt != npt))
    {
        XmhColorTextViewSetFont(nw, nfam ? nfam : "monospace", (double)(npt > 0.0f ? npt : 11.0f));
        do_recompute = True;
        do_redraw    = True;
    }

    // String changes
    const char *ostr = ow->ctvtext.string ? ow->ctvtext.string : "";
    const char *nstr = nw_->ctvtext.string ? nw_->ctvtext.string : "";
    if (strcmp(ostr, nstr) != 0)
    {
        XmhColorTextViewSetString(ctx->textWidget, nstr);
        do_recompute = True;
        do_redraw    = True;
    }

    // If rows/columns changed, just trigger a geometry refresh (parent will query)
    if (ow->ctvtext.rows    != nw_->ctvtext.rows || ow->ctvtext.columns != nw_->ctvtext.columns)
    {
        // Optionally update current widget size to the new preference
        int lh = (ctx->line_height > 0 ? ctx->line_height : 16);
        int cw = ctx->cellwidth;
        Dimension wpref = (nw_->ctvtext.columns > 0) ? (Dimension)(nw_->ctvtext.columns * cw) : 0;
        Dimension hpref = (nw_->ctvtext.rows    > 0) ? (Dimension)(nw_->ctvtext.rows    * lh) : 0;
        if (wpref && hpref)
            XtVaSetValues(nw, XmNwidth, wpref, XmNheight, hpref, NULL);
        else if (wpref)
            XtVaSetValues(nw, XmNwidth, wpref, NULL);
         else if (hpref)
            XtVaSetValues(nw, XmNheight, hpref, NULL);

        // Not strictly necessary to redraw, but harmless
        do_redraw = True;
    }

    if (do_recompute)
        compute_lines(ctx);
    if (do_redraw)
        queue_redraw(ctx);

    return False;
}

static void DoExpose(Widget w, XEvent *event, Region)
{
    if (!event || event->type != Expose)
        return;

    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    Dimension ww = 0, hh = 0;
    XtVaGetValues(w, XmNwidth, &ww, XmNheight, &hh, NULL);

    XExposeEvent ex = event->xexpose;
    ex.x      = 0;
    ex.y      = 0;
    ex.width  = ww;
    ex.height = hh;

    draw_expose(ctx, &ex);
}

static XtGeometryResult QueryGeometry(Widget w, XtWidgetGeometry *in, XtWidgetGeometry *out)
{
    CtvTextRec *tw = (CtvTextRec*)w;
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return XtGeometryNo;

    compute_lines(ctx);

    int lh = (ctx->line_height > 0 ? ctx->line_height : 16);
    int cw = ctx->cellwidth;

    Dimension prefW = (tw->ctvtext.columns > 0)
    ? (Dimension)(tw->ctvtext.columns * cw)
    : (Dimension)((ctx->max_line_px > 0) ? ctx->max_line_px : 1);

    // Height is viewport (rows), not full file height
    Dimension prefH = (tw->ctvtext.rows > 0)
    ? (Dimension)(tw->ctvtext.rows * lh)
    : (Dimension)(25 * lh);   // sensible default

    out->request_mode = CWWidth | CWHeight;
    out->width  = prefW;
    out->height = prefH;

    if (in != 0 &&
        (in->request_mode & CWWidth)  && in->width  == prefW &&
        (in->request_mode & CWHeight) && in->height == prefH)
        return XtGeometryYes;

    return XtGeometryAlmost;
}

static void update_scrollbars(CtvCtx *ctx)
{
    if (!ctx)
        return;

    // Query current widget size (viewport)
    Dimension vw = 0, vh = 0;
    XtVaGetValues(ctx->textWidget,
                  XmNwidth,  &vw,
                  XmNheight, &vh,
                  NULL);
    ctx->viewport_width  = vw;
    ctx->viewport_height = vh;

    int lh = (ctx->line_height > 0) ? ctx->line_height : 16;
    int visible_lines = get_visible_lines(ctx);

    // ---------- Vertical ----------
    if (ctx->vbar && lh > 0)
    {
        int topLine = topLine_from_scroll(ctx, ctx->line_count, visible_lines);
        scroll_from_topLine(ctx, topLine, ctx->line_count, visible_lines);
    }

    // ---------- Horizontal: units = pixels ----------
    if (ctx->hbar)
    {
        int visible_px = (int)vw;
        if (visible_px <= 0)
            visible_px = 1;

        int max = ctx->width_px;     // content width

        if (max <= visible_px)
        {
            // Content fits: reset and hide hbar
            if (XtIsManaged(ctx->hbar))
                XtUnmanageChild(ctx->hbar);

            XtVaSetValues(ctx->hbar,
                          XmNminimum,       0,
                          XmNmaximum,       1,
                          XmNsliderSize,    1,
                          XmNvalue,         0,
                          XmNincrement,     10,
                          XmNpageIncrement, visible_px,
                          NULL);
            return;
        }

        if (!XtIsManaged(ctx->hbar))
            XtManageChild(ctx->hbar);

        int slider = visible_px;
        int value  = 0;
        XtVaGetValues(ctx->hbar, XmNvalue, &value, NULL);

        int upper = std::max(0, max - slider);
        value = std::max(0, std::min(value, upper));

        // Single atomic update: no inconsistent (value, max, slider) combo
        XtVaSetValues(ctx->hbar,
                      XmNminimum,       0,
                      XmNmaximum,       max,
                      XmNsliderSize,    slider,
                      XmNvalue,         value,
                      XmNincrement,     10,
                      XmNpageIncrement, visible_px,
                      NULL);
    }
}

static void vscrollCB(Widget, XtPointer client, XtPointer)
{
    CtvCtx *ctx = (CtvCtx*)client;
    if (!ctx)
        return;

    queue_redraw(ctx);
}

static void hscrollCB(Widget, XtPointer client, XtPointer)
{
    CtvCtx *ctx = (CtvCtx*)client;
    if (!ctx)
        return;

    queue_redraw(ctx);
}

// Track widget (viewport) size and adjust scrollbars
static void configureEH(Widget, XtPointer client, XEvent *ev, Boolean*)
{
    if (!client || !ev || ev->type != ConfigureNotify)
        return;

    CtvCtx *ctx = (CtvCtx*)client;
    ctx->viewport_width  = ev->xconfigure.width;
    ctx->viewport_height = ev->xconfigure.height;
    update_scrollbars(ctx);

    queue_redraw(ctx);
}

// How many full lines fit in the viewport?
static int get_visible_lines(CtvCtx *ctx)
{
    if (!ctx || ctx->line_height <= 0)
        return 1;

    Dimension vh = 0;
    XtVaGetValues(ctx->textWidget, XmNheight, &vh, NULL);

    int lines = std::max(1, (int)vh / ctx->line_height);
    return lines;
}

// Map scrollbar value -> top line, handling both "direct" and "compressed" modes
static int topLine_from_scroll(CtvCtx *ctx, int line_count, int visible_lines)
{
    if (!ctx || !ctx->vbar || line_count <= 0)
        return 0;

    int value = 0, min = 0, max = 0, slider = 0;
    XtVaGetValues(ctx->vbar,
                  XmNvalue,      &value,
                  XmNminimum,    &min,
                  XmNmaximum,    &max,
                  XmNsliderSize, &slider,
                  NULL);

    int N = std::max(line_count, 1);
    int V = std::max(visible_lines, 1);
    int R = std::max(N - V, 0);  // number of scrollable lines

    // Degenerate scrollbar: treat as top = 0
    if (max <= min || slider <= 0)
        return 0;

    // "Direct" mode: 1 unit = 1 line
    if (max == N && slider == V)
    {
        value = std::max(0, std::min(R, value));
        return value;
    }

    // "Compressed" mode: map proportionally
    int scroll_range = max - slider;
    if (scroll_range <= 0)
        return 0;

    value = std::max(0, std::min(scroll_range, value));

    if (R <= 0)
        return 0;

    double frac = (double)value / (double)scroll_range;
    int top = (int)(frac * R + 0.5); // round to nearest
    top = std::max(0, std::min(R, top));
    return top;
}

static void scroll_from_topLine(CtvCtx *ctx, int topLine,
                                int line_count, int visible_lines)
{
    if (!ctx || !ctx->vbar)
        return;

    const double MIN_FRAC  = 0.05;
    const int    MAX_UNITS = 2000;

    int N = std::max(line_count, 1);
    int V = std::max(visible_lines, 1);

    // If content fits -> no scrolling -> hide vbar
    if (N <= V)
    {
        if (XtIsManaged(ctx->vbar))
            XtUnmanageChild(ctx->vbar);
        return;
    }

    if (!XtIsManaged(ctx->vbar))
        XtManageChild(ctx->vbar);

    int R = N - V;
    topLine = std::max(0, std::min(topLine, R));

    double ratio = (double)V / (double)N;

    if (ratio >= MIN_FRAC)
    {
        // Direct mode: 1 unit = 1 line
        int min    = 0;
        int max    = N;
        int slider = V;
        int value  = std::max(min, std::min(topLine, max - slider));

        XtVaSetValues(ctx->vbar,
                      XmNminimum,       min,
                      XmNmaximum,       max,
                      XmNsliderSize,    slider,
                      XmNvalue,         value,
                      XmNincrement,     1,
                      XmNpageIncrement, V,
                      NULL);
        return;
    }

    // Compressed mode
    int max_units   = MAX_UNITS;
    int min_slider  = (int)(MIN_FRAC * max_units + 0.5);
    if (min_slider < 1)
        min_slider = 1;
    if (min_slider >= max_units)
        min_slider = max_units - 1;

    int slider       = min_slider;
    int scroll_range = std::max(max_units - slider, 1);
    int value        = 0;

    if (R > 0)
    {
        double frac = (double)topLine / (double)R;
        value = (int)(frac * scroll_range + 0.5);
        value = std::max(0, std::min(value, scroll_range));
    }

    XtVaSetValues(ctx->vbar,
                  XmNminimum,       0,
                  XmNmaximum,       max_units,
                  XmNsliderSize,    slider,
                  XmNvalue,         value,
                  XmNincrement,     1,
                  XmNpageIncrement, V,
                  NULL);
}

//===============================================================================
// Public API: creation updated to build XmhColorTextView inside XmScrolledWindow
//===============================================================================

/*!
 * \brief Set the viewer content from a UTF-8 string.
 * \param w  XmhColorTextView widget returned by CreateSVScrolledText().
 * \param s   UTF-8 text (copied). If NULL, treated as empty.
 */
void XmhColorTextViewSetString(Widget w, const char *s)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    free_text(ctx);
    ctx->text = strdup(s ? s : "");
    ctx->text_len = (Utf8Pos)strlen(ctx->text);
    ctx->caret     = 0;
    ctx->sel_start = 0;
    ctx->sel_end   = 0;
    ctx->has_sel   = 0;
    ctx->goal_x    = -1;
    compute_lines(ctx);
    queue_redraw(ctx);
}

void XmhColorTextViewEnableGutter(Widget w, Boolean enable)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    ctx->gutter_enabled = enable;
}

void XmhColorTextViewDarkMode(Widget w, Boolean enable)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    ctx->dark_mode = enable;

    if (!XtIsRealized(ctx->textWidget))
        return;

    Display *dpy = XtDisplay(ctx->textWidget);
    if (!dpy)
        return;

    free_palette(dpy, ctx);
    if (!ctx->visual || !ctx->cmap)
        ensure_backbuffer(ctx);
    alloc_default_palette(dpy, ctx->visual, ctx->cmap, ctx);
    queue_redraw(ctx);
}

/*!
 * \brief Retrieve a heap-allocated copy of the current text.
 * \param w XmhColorTextView widget.
 * \return Newly allocated NUL-terminated UTF-8 string (caller must free).
 */
char* XmhColorTextViewGetString(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx || !ctx->text)
        return strdup("");
    return strdup(ctx->text);
}

/*!
 * \brief Get the last byte position in the buffer.
 * \param w XmhColorTextView widget.
 * \return Byte length of the current text.
 */
Utf8Pos XmhColorTextViewGetLastPosition(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    return ctx ? ctx->text_len : 0;
}

void XmhColorTextViewSetTokens(Widget w, const XmhColorToken *tokens, int count)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    ctx->tokens.clear();

    if (!tokens || count <= 0 || !ctx->text)
    {
        queue_redraw(ctx);
        return;
    }

    Utf8Pos text_len = ctx->text_len;

    // 1) Copy & clamp to text range, discard empty/invalid
    std::vector<XmhColorToken> tmp;
    tmp.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        Utf8Pos s = std::max(0, tokens[i].start);
        int     l = tokens[i].len;
        if (l <= 0 || s >= text_len)
            continue;

        Utf8Pos e = std::min(s + l, text_len);
        int nl = (int)(e - s);
        if (nl <= 0)
            continue;

        XmhColorToken tok = tokens[i];
        tok.start = s;
        tok.len   = nl;
        tmp.push_back(tok);
    }

    if (tmp.empty())
    {
        queue_redraw(ctx);
        return;
    }

    // 2) Sort by start
    std::stable_sort(tmp.begin(), tmp.end(),
              [](const XmhColorToken &a, const XmhColorToken &b) {
                  return a.start < b.start;
              });

    // 3) Overlay: later tokens override earlier tokens in overlaps
    //    Result: non-overlapping, sorted by start.
    std::vector<XmhColorToken> out;
    out.reserve(tmp.size());

    for (const XmhColorToken &newTok : tmp)
    {
        Utf8Pos ns = newTok.start;
        Utf8Pos ne = ns + newTok.len;

        // Remove / trim any previous tokens that overlap [ns, ne)
        // Because 'out' is kept non-overlapping and sorted by start, this
        // is a simple backward scan.
        XmhColorToken rightTail{};
        bool haveRightTail = false;

        while (!out.empty())
        {
            XmhColorToken &prev = out.back();
            Utf8Pos ps = prev.start;
            Utf8Pos pe = ps + prev.len;

            if (pe <= ns)
                break; // prev ends before new token -> no overlap

            // Overlap exists: [max(ps,ns), min(pe,ne))

            if (ns <= ps && ne >= pe)
            {
                // new token fully covers prev -> drop prev
                out.pop_back();
                continue;
            }

            if (ns <= ps && ne < pe)
            {
                // new token covers the left part of prev
                // keep only the tail after new token
                prev.start = ne;
                prev.len   = (int)(pe - ne);
                break;
            }

            if (ps < ns && pe <= ne)
            {
                // new token covers the right part of prev
                // shrink prev to its left part [ps, ns)
                prev.len = (int)(ns - ps);
                break;
            }

            // new token is strictly inside prev: ps < ns < ne < pe
            // Keep left part of prev [ps, ns) and right part [ne, pe).
            // Left is kept by shrinking prev; right is saved as tail.
            prev.len = (int)(ns - ps);

            rightTail = prev;
            rightTail.start = ne;
            rightTail.len   = (int)(pe - ne);
            haveRightTail   = true;

            break;
        }

        // Insert the new overriding token
        out.push_back(newTok);

        // If we had a right tail from the last overlapped prev, append it;
        // it starts after 'newTok', so order (prev-left, newTok, rightTail) is preserved.
        if (haveRightTail)
            out.push_back(rightTail);
    }

    #ifdef DEBUG
    // sanity check: no overlaps remain
    for (size_t i = 1; i < out.size(); ++i)
    {
        Utf8Pos prev_e = out[i-1].start + out[i-1].len;
        Utf8Pos cur_s  = out[i].start;
        assert(cur_s >= prev_e);
    }
    #endif

    ctx->tokens.swap(out);
    queue_redraw(ctx);
}

/*!
 * \brief Get current selection range, if any.
 * \param w    XmhColorTextView widget.
 * \param left  Out: selection start byte (may be NULL).
 * \param right Out: selection end byte (may be NULL).
 * \return 1 if a selection exists, 0 otherwise.
 */
int XmhColorTextViewGetSelectionPosition(Widget w, Utf8Pos *left, Utf8Pos *right)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx || !ctx->has_sel)
        return 0;

    if (left)
        *left = ctx->sel_start;
    if (right)
        *right = ctx->sel_end;
    return 1;
}

/*!
 * \brief Clear the selection.
 * \param w   XmhColorTextView widget.
 * \param time Ignored (Motif/Xt signature compatibility).
 */
void XmhColorTextViewClearSelection(Widget w, Time time)
{
    (void)time;
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    ctx->has_sel = 0;
    ctx->sel_start = ctx->sel_end = ctx->caret;
    queue_redraw(ctx);
}

char *XmhColorTextViewGetSelection(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx || !ctx->has_sel || ctx->sel_end <= ctx->sel_start || !ctx->text)
        return NULL;

    Utf8Pos start = ctx->sel_start;
    Utf8Pos end   = ctx->sel_end;
    long len      = (long)(end - start);
    if (len <= 0)
        return NULL;

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf)
        return NULL;

    memcpy(buf, ctx->text + start, (size_t)len);
    buf[len] = '\0';
    return buf;
}

/*!
 * \brief Set caret (insertion) position.
 * \param w  XmhColorTextView widget.
 * \param pos Byte position (clamped to buffer).
 */
void XmhColorTextViewSetInsertionPosition(Widget w, Utf8Pos pos)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    ctx->caret = std::max(0, std::min(pos, ctx->text_len));
    queue_redraw(ctx);
}

/*!
 * \brief Get the current caret position.
 * \param w XmhColorTextView widget.
 * \return Byte position of the caret.
 */
Utf8Pos XmhColorTextViewGetInsertionPosition(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;

    return ctx->caret;
}


/*!
 * \brief Convert text position to widget coordinates.
 * \param w   XmhColorTextView widget.
 * \param pos  Byte position.
 * \param x    Out: x in pixels (may be NULL).
 * \param y    Out: y in pixels (may be NULL).
 * \return 1 on success, 0 otherwise.
 */
int XmhColorTextViewPosToXY(Widget w, Utf8Pos pos, Position *x, Position *y)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
    {
        if (x)
            *x=0;
        if (y)
            *y=0;
        return 0;
    }

    int xi, yi;
    int ok = pos_to_xy(ctx, pos, &xi, &yi, CTV_COORD_VIEWPORT);

    if (yi<0)
        ok = false;
    if (x!=nullptr)
        *x=xi;
    if (y!=nullptr)
        *y=yi;
    return ok;
}

/*!
 * \brief Convert widget coordinates to nearest text position.
 * \param w XmhColorTextView widget.
 * \param x  X pixel coordinate.
 * \param y  Y pixel coordinate.
 * \return Nearest byte position.
 */
Utf8Pos XmhColorTextViewXYToPos(Widget w, Position x, Position y)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;
    return xy_to_pos(ctx, x, y, CTV_COORD_VIEWPORT);
}

/*! \internal Helper: set a scrollbar value clamped to [min, max - slider] */
static void scrollbar_set_value(Widget sb, int value)
{
    if (!sb) return;

    int min = 0, max = 0, slider = 1, inc = 1, page = 1;
    XtVaGetValues(sb,
                  XmNminimum,      &min,
                  XmNmaximum,      &max,
                  XmNsliderSize,   &slider,
                  XmNincrement,    &inc,
                  XmNpageIncrement,&page,
                  NULL);

    int range = max - min;
    if (range <= 0)
    {
        range  = 1;
        max    = min + range;
        slider = 1;
        XtVaSetValues(sb,
                      XmNmaximum,    max,
                      XmNsliderSize, slider,
                      NULL);
    }
    else if (slider > range)
    {
        slider = range;
        XtVaSetValues(sb, XmNsliderSize, slider, NULL);
    }

    int upper = std::max(min, max - slider);
    value = std::max(min, std::min(value, upper));

    XmScrollBarSetValues(sb, value, slider, inc, page, True);
}

/*! \internal Set horizontal scrollbar value (clamped). */
static void set_hscroll(CtvCtx *ctx, int x)
{
    if (!ctx || !ctx->hbar)
        return;
    scrollbar_set_value(ctx->hbar, x);
}

/*!
 * \brief Scroll so that the given position appears at the top of the view.
 * \param w  XmhColorTextView widget.
 * \param pos Byte position to place at top.
 */
void XmhColorTextViewSetTopCharacter(Widget w, Utf8Pos pos)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    int x,y;
    if (!pos_to_xy(ctx, pos, &x, &y, CTV_COORD_ABSOLUTE))
        return;
    if (ctx->line_height <= 0)
        return;

    int line = y / ctx->line_height;
    int visible_lines = get_visible_lines(ctx);
    scroll_from_topLine(ctx, line, ctx->line_count, visible_lines);

    queue_redraw(ctx);
}

/*!
 * \brief Query the byte position currently at the top of the view.
 * \param w XmhColorTextView widget.
 * \return Byte position near the top-left corner.
 */
Utf8Pos XmhColorTextViewGetTopCharacter(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;

    int visible_lines = get_visible_lines(ctx);
    int topLine = 0;
    if (ctx->vbar)
        topLine = topLine_from_scroll(ctx, ctx->line_count, visible_lines);

    int yval = topLine * ctx->line_height;
    return xy_to_pos(ctx, 0, yval, CTV_COORD_ABSOLUTE);
}

/*!
 * \brief Ensure a position is visible (scrolls minimally).
 * \param w  XmhColorTextView widget.
 * \param pos Byte position to reveal.
 */
void XmhColorTextViewShowPosition(Widget w, Utf8Pos pos)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    int px, py;
    if (!pos_to_xy(ctx, pos, &px, &py, CTV_COORD_ABSOLUTE))
        return;

    int lh = (ctx->line_height > 0) ? ctx->line_height : 16;
    int line = (lh > 0) ? (py / lh) : 0;

    // Vertical: unchanged
    int visible_lines = get_visible_lines(ctx);
    int curTop = 0;
    if (ctx->vbar)
        curTop = topLine_from_scroll(ctx, ctx->line_count, visible_lines);

    int needTop = curTop;
    if (line < curTop)
        needTop = line;
    else if (line >= curTop + visible_lines)
        needTop = line - (visible_lines - 1);

    if (ctx->vbar && needTop != curTop)
        scroll_from_topLine(ctx, needTop, ctx->line_count, visible_lines);

    // Horizontal: px and h are both in content coords now
    if (ctx->hbar)
    {
        int h = 0, sliderH = 1;
        XtVaGetValues(ctx->hbar, XmNvalue, &h, XmNsliderSize, &sliderH, NULL);

        int needH = h;
        if (px < h)
            needH = px;
        else if (px >= h + sliderH)
            needH = px - (sliderH - 40);   // 40px margin

        if (needH < 0)
            needH = 0;

        if (needH != h)
            set_hscroll(ctx, needH);
    }
}

/*!
 * \brief Get the scrolled window that owns the widget.
 * \param da XmhColorTextView widget.
 * \return XmScrolledWindow widget.
 */
Widget XmhColorTextViewGetScrolledWindow(Widget da)
{
    CtvCtx *ctx = get_ctx(da);
    return ctx ? ctx->scrolledWindow : (Widget)nullptr;
}

/*!
 * \brief Attach the scrolled window to all sides of its XmForm parent.
 *
 * If the parent of the scrolled window is an XmForm, this makes the view
 * fill the available space and be resizable.
 *
 * \param da XmhColorTextView widget.
 */
void XmhColorTextViewSetFillParent(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    Widget sw = ctx->scrolledWindow;
    Widget parent = XtParent(sw);
    if (XtIsSubclass(parent, xmFormWidgetClass))
    {
        XtVaSetValues(sw,
            XmNleftAttachment,   XmATTACH_FORM,
            XmNrightAttachment,  XmATTACH_FORM,
            XmNtopAttachment,    XmATTACH_FORM,
            XmNbottomAttachment, XmATTACH_FORM,
            XmNresizable,        True,    // IMPORTANT for height adaption
            NULL);
    }
}

static void update_metrics_from_font(CtvCtx *ctx)
{
    if (!ctx || !ctx->font)
        return;

    ctx->ascent = ctx->font->ascent;
    ctx->descent = ctx->font->descent;
    ctx->line_height = ctx->ascent + ctx->descent;

    Display *dpy = XtDisplayOfObject(ctx->textWidget);
    if (!dpy || !ctx->font)
    {
        ctx->cellwidth =  8; // fallback
        return;
    }

    // Use a representative glyph. For monospaced, any ASCII is fine.
    const FcChar8 M = (FcChar8)'M';
    XGlyphInfo gi;
    XftTextExtentsUtf8(dpy, ctx->font, &M, 1, &gi);
    if (gi.xOff <= 0)
    {
        ctx->cellwidth =  8;
        return;
    }
    ctx->cellwidth = gi.xOff;
}

void XmhColorTextViewSetFontPattern(Widget w, const char *xft_pattern)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    Display *dpy = XtDisplayOfObject(ctx->textWidget);
    if (!dpy)
        return;

    XftFont *old = ctx->font;
    ctx->font = XftFontOpenName(dpy, DefaultScreen(dpy), (xft_pattern && *xft_pattern) ? xft_pattern : "monospace:size=11");
    if (!ctx->font)
        ctx->font = old ? old : XftFontOpenName(dpy, DefaultScreen(dpy), "monospace:size=11");
    else if (old && ctx->font != old)
        XftFontClose(dpy, old);

    if (ctx->font)
    {
        // Family from resolved pattern
        FcChar8 *fam = nullptr;
        if (FcPatternGetString(ctx->font->pattern, FC_FAMILY, 0, &fam) == FcResultMatch && fam)
        {
            free(ctx->font_family);
            ctx->font_family = strdup((char*)fam);
        }

        // Point size from resolved pattern
        double pt = 0.0;
        if (FcPatternGetDouble(ctx->font->pattern, FC_SIZE, 0, &pt) == FcResultMatch && pt > 0.0)
            ctx->font_pt = pt;
    }

    // Clear styled caches and *all* fallbacks (sizes may have changed)
    free_styled_fonts(dpy, ctx);
    clear_fallback_fonts(ctx);

    update_metrics_from_font(ctx);
    compute_lines(ctx);
    queue_redraw(ctx);
}

void XmhColorTextViewSetFont(Widget w, const char *family, double pt)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    if (!family || !*family)
        family = "monospace";
    if (pt <= 0)
        pt = 11.0;

    char pat[128];
    snprintf(pat, sizeof(pat), "%s:size=%.1f", family, pt);
    XmhColorTextViewSetFontPattern(w, pat);
}

int  XmhColorTextViewGetFontSize(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return -1;

    return ctx->font_pt;
}

static Pixel alloc_named_color(Widget w, const char *name)
{
    Display *dpy = XtDisplay(w);
    Colormap cmap = DefaultColormapOfScreen(XtScreen(w));
    XColor scr, exact;
    if (XAllocNamedColor(dpy, cmap, name, &scr, &exact))
        return scr.pixel;
    return BlackPixelOfScreen(XtScreen(w));
}

void XmhColorTextViewSetBackgroundPixel(Widget w, Pixel bg)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    ctx->bg = bg;

    // Child widget background
    XtVaSetValues(ctx->textWidget, XmNbackground, bg, NULL);

    // Clip window (the area showing unused space)
    Widget clip = NULL;
    if (ctx->scrolledWindow)
        XtVaGetValues(ctx->scrolledWindow, XmNclipWindow, &clip, NULL);
    if (clip)
        XtVaSetValues(clip, XmNbackground, bg, NULL);

     // Repaint all
    if (XtIsRealized(ctx->textWidget))
        XClearArea(XtDisplay(ctx->textWidget), XtWindow(ctx->textWidget), 0,0,0,0, True);
    if (clip && XtIsRealized(clip))
        XClearArea(XtDisplay(clip), XtWindow(clip), 0,0,0,0, True);
}

int XmhColorTextViewSetBackgroundName(Widget w, const char *name)
{
    if (!name)
        return 0;
    Pixel px = alloc_named_color(w, name);
    XmhColorTextViewSetBackgroundPixel(w, px);
    return 1;
}

void XmhColorTextViewSelectRange(Widget w, Utf8Pos start, Utf8Pos end)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    if (start > end)
        std::swap(start, end);

    start = std::max(Utf8Pos(0), start);
    end = std::min(end, ctx->text_len);

    // Align to code point boundaries
    start = align_cp_backward(ctx->text, 0, start);
    end   = align_cp_forward(ctx->text, ctx->text_len, end);

    ctx->sel_start = start;
    ctx->sel_end   = end;
    ctx->has_sel   = (end > start);
    ctx->caret     = end;  // caret to end of selection for convenience
    queue_redraw(ctx);
}

void XmhColorTextViewWordBoundsAt(Widget w, Utf8Pos pos, Utf8Pos *outStart, Utf8Pos *outEnd)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx || !ctx->text)
    {
        if (outStart)
            *outStart = 0;
        if (outEnd)
            *outEnd = 0;
        return;
    }
    const char *s = ctx->text;
    Utf8Pos len = ctx->text_len;

    pos = std::max(Utf8Pos(0), std::min(len, pos));

    // Start on a code point boundary
    pos = align_cp_backward(s, 0, pos);

    auto is_word = [](unsigned char c)->bool { return c=='_' || isalnum(c); };

    Utf8Pos L = pos;
    while (L > 0)
    {
        Utf8Pos p = prev_cp(s, 0, L);
        unsigned char c = (unsigned char)s[p];
        if (!is_word(c)) break;
        L = p;
    }

    Utf8Pos R = pos;
    while (R < len)
    {
        unsigned char c = (unsigned char)s[R];
        if (!is_word(c)) break;
        R = next_cp(s, len, R);
    }

    if (outStart)
        *outStart = L;
    if (outEnd)
        *outEnd   = R;
}

int XmhColorTextViewGetVisibleRows(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;
    return get_visible_lines(ctx);
}

int XmhColorTextViewGetVisibleColumns(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;

    // Get visible width: prefer the clip window of the scrolled window
    Dimension width = 0;
    if (ctx->scrolledWindow)
        XtVaGetValues(ctx->scrolledWindow, XmNwidth, &width, NULL);

    // Ensure font metrics / cell width
    ensure_font(ctx);
    int cw = std::max(1, ctx->cellwidth);

    return width / cw;
}

int XmhColorTextViewGetLineHeight(Widget w)
{
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;

    return ctx->line_height;
}

int XmhColorTextViewSetSelection(Widget w, Utf8Pos start, Utf8Pos end, Time time)
{
    (void)time;
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return 0;

    if (start > end) { Utf8Pos t = start; start = end; end = t; }
    if (start < 0) start = 0;
    if (end > ctx->text_len) end = ctx->text_len;

    // Align to UTF-8 codepoint boundaries
    start = align_cp_backward(ctx->text, 0, start);
    end   = align_cp_forward(ctx->text, ctx->text_len, end);

    ctx->sel_start  = start;
    ctx->sel_end    = end;
    ctx->has_sel    = (end > start);
    ctx->sel_anchor = start;  // anchor like XmText
    ctx->caret      = end;    // caret at end of selection

    // Ensure visible and redraw
    XmhColorTextViewShowPosition(w, ctx->caret);
    queue_redraw(ctx);

    return ctx->has_sel ? 1 : 0;
}

Boolean XmhColorTextViewCopy(Widget w, Time time)
{
    CtvCtx *ctx = get_ctx(w);
    return copy_selection_to_clipboard(w, ctx, time);
}


// Implementation of Action functions

static void GrabFocusAction(Widget w, XEvent*, String*, Cardinal*)
{
    XtSetKeyboardFocus(XtParent(w), w);
}

static void ExtendEndAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    (void)params;
    (void)num_params;

    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    // 1. Move caret to pointer position, if we have coordinates
    if (event)
    {
        int x = 0, y = 0;

        switch (event->type)
        {
            case ButtonPress:
            case ButtonRelease:
                x = event->xbutton.x;
                y = event->xbutton.y;
                break;
            case MotionNotify:
                x = event->xmotion.x;
                y = event->xmotion.y;
                break;
            default:
                // For other event types we don't know the pointer position;
                // just leave caret where it is.
                break;
        }

        Utf8Pos p = xy_to_pos(ctx, x, y, CTV_COORD_VIEWPORT);
        ctx->caret = p;
    }

    // 2. Commit the selection: stop dragging/extending
    ctx->dragging = 0;

    // "gain_primary_callback" is called it here to indicate that the selection
    // is finalized and PRIMARY should be owned.
    CtvTextRec *tw = (CtvTextRec*)w;
    if (tw->ctvtext.gain_primary_callback)
        XtCallCallbackList(ctx->textWidget, tw->ctvtext.gain_primary_callback, nullptr);

    queue_redraw(ctx);
}

static void SelectAllAction(Widget w, XEvent*, String*, Cardinal*)
{
    // select all
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;

    ctx->sel_start = 0;
    ctx->sel_end = ctx->text_len;
    ctx->has_sel = true;
    ctx->caret = ctx->text_len;

    queue_redraw(ctx);
}

static void EndOfLineAction(Widget w, XEvent*, String*, Cardinal*)
{
    //move caret to end of line
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    move_home_end(ctx, true,  false, false);
}

static void NextPageAction(Widget w, XEvent*, String*, Cardinal*)
{
    //scroll down one page
    // move caret only if it leaves the viewport
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    //sv_move_v(ctx, +sv_lines_per_page(ctx), false, true);
    int lines = get_visible_lines(ctx);
    scroll_v_by_lines(ctx, +lines);
}

static void PreviousPageAction(Widget w, XEvent*, String*, Cardinal*)
{
    // scroll up one page
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    // sv_move_v(ctx, -sv_lines_per_page(ctx), false, true);
    int lines = get_visible_lines(ctx);
    scroll_v_by_lines(ctx, -lines);
}

static void CopyClipboardAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    (void)params;
    (void)num_params;

    Display *dpy = XtDisplay(w);
    Time t = event_time(event, dpy);

    CtvCtx *ctx = get_ctx(w);
    (void)copy_selection_to_clipboard(w, ctx, t);
}

static void EmptyAction(Widget , XEvent*, String*, Cardinal*)
{
    return;
}

static void BeginningOfLineAction(Widget w, XEvent*, String*, Cardinal*)
{
    // move caret to beginning of line
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    move_home_end(ctx, false, false, false);
}

static void PreviousLineAction(Widget w, XEvent*, String*, Cardinal*)
{
    // move caret up
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    move_v(ctx, -1, false);
}

static void NextLineAction(Widget w, XEvent*, String*, Cardinal*)
{
    // move caret down
    CtvCtx *ctx = get_ctx(w);
    if (!ctx)
        return;
    move_v(ctx, +1, false);
}

