// SourceWidget.h
#ifndef SOURCEWIDGET_TEXT_H
#define SOURCEWIDGET_TEXT_H

#include <Xm/Xm.h>
#include <cstdint>
#include <vector>


/*!
 * \brief Byte offset in UTF-8.
 * \note Offsets refer to bytes, not code points.
 */
typedef int32_t Utf8Pos;          // byte offset in UTF-8

// Style flags for tokens
enum {
    XMH_STYLE_NONE   = 0,
    XMH_STYLE_BOLD   = 1 << 0,
    XMH_STYLE_ITALIC = 1 << 1
};

struct XmhColorToken
{
    Utf8Pos start = 0;   // byte start
    int    len = 0;     // bytes
    int    color = 0;   // palette index (0 = default)
    int    style = 0;   // bitmask: XMH_STYLE_*
};

// Widget resources for XtVaSetValues/XtVaGetValues
#define XmhNstring     ((char*)"string")
#define XmhNfontFamily ((char*)"fontFamily")
#define XmhNfontSize   ((char*)"fontSize")
#define XmhNcolumns ((char*)"columns")
#define XmhNrows    ((char*)"rows")
#define XmhNtopCharacter ((char*)"topCharacter")
#define XmhNviewportChangedCallback ((char*)"viewportChangedCallback")

bool XmhIsColorTextView(Widget w);

// Create a scrolled, colored text viewer as a real widget (XmPrimitive subclass).
// Returns the XmhColorTextView widget.
Widget CreateXmhColorTextView(Widget parent, const char *name, Arg *args, Cardinal n);

// Content
void   XmhColorTextViewSetString(Widget w, const char *utf8);
char*  XmhColorTextViewGetString(Widget w);               // caller frees with free()
Utf8Pos XmhColorTextViewGetLastPosition(Widget w);

void XmhColorTextViewEnableGutter(Widget w, Boolean enable);
void XmhColorTextViewDarkMode(Widget w, Boolean enable);

// Coloring (syntax highlighting)
void   XmhColorTextViewSetTokens(Widget w, const XmhColorToken *tokens, int count);

// Selection
int    XmhColorTextViewGetSelectionPosition(Widget w, Utf8Pos *left, Utf8Pos *right);
void   XmhColorTextViewClearSelection(Widget w, Time time);
char *XmhColorTextViewGetSelection(Widget w);

// Caret (insertion position)
void   XmhColorTextViewSetInsertionPosition(Widget w, Utf8Pos pos);
Utf8Pos XmhColorTextViewGetInsertionPosition(Widget w);

// Mapping
int    XmhColorTextViewPosToXY(Widget w, Utf8Pos pos, Position *x, Position *y);
Utf8Pos XmhColorTextViewXYToPos(Widget w, Position x, Position y);

// Scrolling controls
void   XmhColorTextViewSetTopCharacter(Widget w, Utf8Pos pos);
Utf8Pos XmhColorTextViewGetTopCharacter(Widget w);
void   XmhColorTextViewShowPosition(Widget w, Utf8Pos pos);

// Accessors and sizing helpers
Widget  XmhColorTextViewGetScrolledWindow(Widget w);  // returns the scrolled window parent
void    XmhColorTextViewSetFillParent(Widget w);// attach SW to all sides (if parent is XmForm)

// Font control
void XmhColorTextViewSetFont(Widget w, const char *family, double pt); // e.g., "monospace", 12.0
void XmhColorTextViewSetFontPattern(Widget w, const char *xft_pattern); // e.g., "monospace:size=12"
int  XmhColorTextViewGetFontSize(Widget w);

void XmhColorTextViewSetBackgroundPixel(Widget w, Pixel bg);
int  XmhColorTextViewSetBackgroundName(Widget w, const char *name);// returns 1 on success

int XmhColorTextViewGetVisibleRows(Widget w);
int XmhColorTextViewGetVisibleColumns(Widget w);
int XmhColorTextViewGetLineHeight(Widget w);

// Caret/selection color control
void XmhColorTextViewSelectRange(Widget w, Utf8Pos start, Utf8Pos end);
void XmhColorTextViewWordBoundsAt(Widget w, Utf8Pos pos, Utf8Pos *start, Utf8Pos *end);

int XmhColorTextViewSetSelection(Widget w, Utf8Pos start, Utf8Pos end, Time time);

Boolean XmhColorTextViewCopy(Widget w, Time time);


#endif // SOURCEWIDGET_TEXT_H
