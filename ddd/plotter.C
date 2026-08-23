// $Id$ -*- C++ -*-
// Create a plotter interface

// Copyright (C) 1998 Technische Universitaet Braunschweig, Germany.
// Copyright (C) 2001, 2003 Free Software Foundation, Inc.
// Written by Andreas Zeller <zeller@gnu.org>
//        and Stefan Eickeler <eickeler@gnu.org>.
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

char plotter_rcsid[] = 
    "$Id$";

#include "plotter.h"

#include "base/assert.h"
#include "x11/charsets.h"
#include "base/cook.h"
#include "ddd.h"
#include "exit.h"
#include "x11/findParent.h"
#include "file.h"
#include "filetype.h"
#include "fonts.h"
#include "post.h"
#include "print.h"
#include "regexps.h"
#include "simpleMenu.h"
#include "status.h"
#include "base/strclass.h"
#include "string-fun.h"
#include "tempfile.h"
#include "x11/verify.h"
#include "version.h"
#include "wm.h"
#include "AppData.h"
#include "Command.h"
#include "x11/Delay.h"
#include "x11/DeleteWCB.h"
#include "HelpCB.h"
#include "motif/MakeMenu.h"
#include "DispValue.h"
#include "DataDisp.h"
#include "x11/DestroyCB.h"
#include "agent/TimeOut.h"
#include "darkmode.h"
#include "scrollbar.h"

#include <Xm/Command.h>
#include <Xm/MainW.h>
#include <Xm/MessageB.h>
#include <Xm/AtomMgr.h>
#include <Xm/FileSB.h>
#include <Xm/DrawingA.h>
#include <Xm/ScrolledW.h>
#include <Xm/SelectioB.h>
#include <Xm/ScrollBar.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>
#include <Xm/RowColumn.h>
#include <Xm/Label.h>
#include <Xm/MenuShell.h>
#include <Xm/CascadeB.h>

#include <stdio.h>
#include <fstream>
#include <vector>
#include <sstream>
#include <locale>
#include <cmath>

static void TraceInputHP (Agent *source, void *, void *call_data);
static void TraceOutputHP(Agent *source, void *, void *call_data);
static void TraceErrorHP (Agent *source, void *, void *call_data);
static void PlotterNotFoundHP(Agent *source, void *, void *call_data);

static void CancelPlotCB(Widget, XtPointer, XtPointer);

static void SelectPlotCB(Widget, XtPointer, XtPointer);
static void SelectAndPrintPlotCB(Widget, XtPointer, XtPointer);
static void SaveImageCB(Widget, XtPointer, XtPointer);
static void SaveImageOkCB(Widget, XtPointer, XtPointer);

static void ReplotCB(Widget, XtPointer, XtPointer);
static void PlotCommandCB(Widget, XtPointer, XtPointer);

static void ToggleOptionCB(Widget, XtPointer, XtPointer);
static void ToggleLogscaleCB(Widget, XtPointer, XtPointer);
static void SetStyleCB(Widget, XtPointer, XtPointer);
static void SetContourCB(Widget, XtPointer, XtPointer);

struct PlotWindowInfo {
    DispValue *source = nullptr;            // The source we depend upon
    string window_name = "";                // The window name
    PlotAgent *plotter = nullptr;	    // The current Gnuplot instance
    Widget shell = 0;	                    // The shell we're in
    Widget gnuplot = 0;                     // The Gnuplot window
    Widget command = 0;                     // Command widget
    Widget command_dialog = 0;              // Command dialog

    string settings = "";                   // Plot settings
    XtIntervalId settings_timer = 0;        // Wait for settings
    string settings_file = "";              // File to get settings from
    StatusDelay *settings_delay = nullptr;  // Delay while getting settings
    bool   have_gpval = false;
    double x_min = 0, x_max = 0;
    double y_min = 0, y_max = 0;
    double term_xmin = 0, term_xmax = 0;
    double term_ymin = 0, term_ymax = 0;
    double term_xsize = 0, term_ysize = 0;
    double term_scale = 1;
    string gnuplot_err_buf;                 // to store incomplete messages from gnuplot


    // workaround for the floating graph in the gnuplot window
    // Calibration offsets in data coordinates
    bool   have_offset = false;
    double x_offset = 0.0;
    double y_offset = 0.0;
};

static Widget save_image_dialog = 0;
static PlotWindowInfo *save_image_plot = 0;

//-------------------------------------------------------------------------
// Menus
//-------------------------------------------------------------------------

static Widget save_image_w;
static MMDesc file_menu[] = 
{
    { "command", MMPush, { PlotCommandCB, 0 }, 0, 0, 0, 0 },
    MMSep,
    { "replot",  MMPush, { ReplotCB, 0 }, 0, 0, 0, 0 },
    { "print",   MMPush, { SelectAndPrintPlotCB, 0 }, 0, 0, 0, 0 },
    { "save_image",   MMPush, { SaveImageCB, 0 }, 0, &save_image_w, 0, 0 },
    MMSep,
    { "close",   MMPush, { CancelPlotCB, 0 }, 0, 0, 0, 0 },
    MMEnd
};

static MMDesc view_menu[] = 
{
    { "border",    MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    { "timestamp",      MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    MMSep,
    { "grid",      MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    { "xzeroaxis", MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    { "yzeroaxis", MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    MMEnd
};

static MMDesc contour_menu[] = 
{
    { "base",      MMMenuToggle, { SetContourCB, 0 }, 0, 0, 0, 0 },
    { "surface",   MMMenuToggle, { SetContourCB, 0 }, 0, 0, 0, 0 },
    MMEnd
};

static MMDesc scale_menu[] = 
{
    { "logscale",  MMMenuToggle, { ToggleLogscaleCB, 0 }, 0, 0, 0, 0 },
    MMSep,
    { "xtics",     MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    { "ytics",     MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    { "ztics",     MMMenuToggle, { ToggleOptionCB, 0 }, 0, 0, 0, 0 },
    MMEnd
};

static MMDesc plot_menu[] = 
{
    { "points",         MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "lines",          MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "lines3d",        MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "linespoints",    MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "linespoints3d",  MMMenuToggle | MMUnmanaged,
                                  { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "impulses",       MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "dots",           MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "steps2d",        MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    { "boxes2d",        MMMenuToggle, { SetStyleCB, 0 }, 0, 0, 0, 0 },
    MMEnd
};

static MMDesc menubar[] = 
{
    { "file",     MMMenu,          MMNoCB, file_menu,        0, 0, 0 },
    { "edit",     MMMenu,          MMNoCB, simple_edit_menu, 0, 0, 0 },
    { "plotView", MMMenu,          MMNoCB, view_menu,        0, 0, 0 },
    { "plot",     MMRadioMenu,     MMNoCB, plot_menu,        0, 0, 0 },
    { "scale",    MMMenu,          MMNoCB, scale_menu,       0, 0, 0 },
    { "contour",  MMMenu,          MMNoCB, contour_menu,     0, 0, 0 },
    { "help",     MMMenu | MMHelp, MMNoCB, simple_help_menu, 0, 0, 0 },
    MMEnd
};


static void configure_plot(PlotWindowInfo *plot);


//-------------------------------------------------------------------------
// Plotter commands
//-------------------------------------------------------------------------

static void send(PlotWindowInfo *plot, const string& cmd)
{
    data_disp->select(plot->source);
    plot->plotter->write(cmd.chars(), cmd.length());
}

static void send_and_replot(PlotWindowInfo *plot, string cmd)
{
    if (cmd.matches(rxwhite))
	return;

    if (!cmd.contains('\n', -1))
	cmd += "\n";
    if (cmd.contains("help", 0))
	cmd += "\n";		// Exit `help'
    else
	cmd += "replot\n";

    send(plot, cmd);
}


//-------------------------------------------------------------------------
// Set up menu
//-------------------------------------------------------------------------

static void slurp_file(const string& filename, string& target)
{
    std::ifstream is(filename.chars());
    if (is.bad())
    {
	target = "";
	return;
    }

    std::ostringstream s;
    int c;
    while ((c = is.get()) != EOF)
	s << (unsigned char)c;

    target = s;
}

static void GetPlotSettingsCB(XtPointer client_data, XtIntervalId *id)
{
    (void) id;			// Use it
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;

    assert(plot->settings_timer == *id);
    plot->settings_timer = 0;

    // Check for settings file to be created
    string settings;
    slurp_file(plot->settings_file, settings);

    if (settings.contains("set zero"))
    {
	// Settings are complete
	unlink(plot->settings_file.chars());
	plot->settings = settings;

	configure_plot(plot);

	delete plot->settings_delay;
	plot->settings_delay = nullptr;
    }
    else
    {
	// Try again in 500 ms
	plot->settings_timer = 
	    XtAppAddTimeOut(XtWidgetToApplicationContext(plot->shell), 500, 
			    GetPlotSettingsCB, XtPointer(plot));
    }
}

static void configure_options(PlotWindowInfo *plot, MMDesc *menu, 
			      const string& settings)
{
    for (int i = 0; menu[i].name != 0; i++)
    {
	if ((menu[i].type & MMTypeMask) != MMMenuToggle)
	    continue;

	string name = menu[i].name;

	const string s1 = "*" + name;
	Widget w = XtNameToWidget(plot->shell, s1.chars());
	XtCallbackProc callback = menu[i].callback.callback;

	bool set = false;
	if (callback == ToggleOptionCB)
	{
	    set = settings.contains("\nset " + name);
            if (settings.contains("\nset " + name + " \"\""))
                set = false;

	}
	else if (callback == SetContourCB)
	{
	    if (name == "base")
		set = settings.contains("\nset contour base\n") ||
		    settings.contains("\nset contour both\n");
	    else if (name == "surface")
		set = settings.contains("\nset contour surface\n") ||
		    settings.contains("\nset contour both\n");
	}
	else if (callback == ToggleLogscaleCB)
	{
	    set = settings.contains("\nset logscale");
	}

	XmToggleButtonSetState(w, set, False);
    }
}

static void configure_plot(PlotWindowInfo *plot)
{
    if (plot->plotter == 0)
	return;

    int ndim = plot->plotter->dimensions();

    bool image = plot->plotter->isImage();

    Widget plotw = XtNameToWidget(plot->shell, "*plot");
    Widget scalew = XtNameToWidget(plot->shell, "*scale");
    Widget contourw = XtNameToWidget(plot->shell, "*contour");
    XtSetSensitive(plotw, !image);
    XtSetSensitive(scalew, !image);
    XtSetSensitive(contourw, !image && ndim >= 3);

    // Set up plot menu
    int i;
    for (i = 0; plot_menu[i].name != 0; i++)
    {
	if ((plot_menu[i].type & MMTypeMask) != MMMenuToggle)
	    continue;

	string name = plot_menu[i].name;

	const string s1 = "*" + name;
	Widget w = XtNameToWidget(plot->shell, s1.chars());

	if (name.contains("2d", -1))
	    XtSetSensitive(w, ndim == 2);
	else if (name.contains("3d", -1))
	    XtSetSensitive(w, ndim >= 3);
	else
	    XtSetSensitive(w, ndim >= 2);
    }

    // Log scale is available only iff all values are non-negative
    Widget logscale = XtNameToWidget(plot->shell, "*logscale");
    XtSetSensitive(logscale, True);

    // Axes can be toggled in 2d mode only
    Widget grid = XtNameToWidget(plot->shell, "*grid");
    Widget xzeroaxis = XtNameToWidget(plot->shell, "*xzeroaxis");
    Widget yzeroaxis = XtNameToWidget(plot->shell, "*yzeroaxis");
    XtSetSensitive(grid, !image);
    XtSetSensitive(xzeroaxis, ndim <= 2 && !image);
    XtSetSensitive(yzeroaxis, ndim <= 2 && !image);

    // Z Tics are available in 3d mode only
    Widget ztics = XtNameToWidget(plot->shell, "*ztics");
    XtSetSensitive(ztics, ndim >= 3);

    // Contour drawing is available in 3d mode only
    Widget base    = XtNameToWidget(plot->shell, "*base");
    Widget surface = XtNameToWidget(plot->shell, "*surface");
    XtSetSensitive(base,    ndim >= 3);
    XtSetSensitive(surface, ndim >= 3);

    // The remainder requires settings
    if (plot->settings.empty())
    {
	// No settings yet
	if (plot->settings_timer == 0)
	{
	    plot->settings_delay = new StatusDelay("Retrieving Plot Settings");

	    // Save settings...
	    plot->settings_file = tempfile();
	    string cmd = "save " + quote(plot->settings_file) + "\n";
	    send(plot, cmd);

	    // ...and try again in 250ms
	    plot->settings_timer = 
		XtAppAddTimeOut(XtWidgetToApplicationContext(plot->shell), 250,
				GetPlotSettingsCB, XtPointer(plot));
	}

	return;
    }

    configure_options(plot, view_menu,    plot->settings);
    configure_options(plot, contour_menu, plot->settings);
    configure_options(plot, scale_menu,   plot->settings);

    XtSetSensitive(save_image_w, image);

    // Get style
    for (i = 0; plot_menu[i].name != 0; i++)
    {
	if ((plot_menu[i].type & MMTypeMask) != MMMenuToggle)
	    continue;

	string name = plot_menu[i].name;

	const string s1 = "*" + name;
	Widget w = XtNameToWidget(plot->shell, s1.chars());

	bool set = plot->settings.contains("\nset style data " + name + "\n");
	XmToggleButtonSetState(w, set, False);
    }
}



//-------------------------------------------------------------------------
// Decoration stuff
//-------------------------------------------------------------------------

// Start plot
static void popup_plot_shell(PlotWindowInfo *plot)
{
    // Command and export dialogs are not needed (yet)
    if (plot->command_dialog != 0)
        XtUnmanageChild(plot->command_dialog);

    // Pop up shell
    XtSetSensitive(plot->shell, True);
    XtPopup(plot->shell, XtGrabNone);
    wait_until_mapped(plot->gnuplot);

    if (XtIsRealized(plot->shell))
        XMapWindow(XtDisplay(plot->shell), XtWindow(plot->shell));
    raise_shell(plot->shell);

    if (app_data.raise_when_ready)
        suppress_auto_raise(plot->shell, 2000);
}

// Cancel plot
static void popdown_plot_shell(PlotWindowInfo *plot)
{
    static bool entered = false;
    if (entered)
	return;

    entered = true;

    // Manage dialogs
    if (plot->command_dialog != nullptr)
	XtUnmanageChild(plot->command_dialog);

    if (plot->shell != nullptr && XtWindow(plot->shell)!=0)
    {
	XWithdrawWindow(XtDisplay(plot->shell), XtWindow(plot->shell),
			XScreenNumberOfScreen(XtScreen(plot->shell)));
	XtPopdown(plot->shell);

	// XtPopdown may not withdraw an iconified shell.  Hence, make
	// sure the shell really becomes disabled.
	XtSetSensitive(plot->shell, False);
    }

    // Manage settings
    if (plot->settings_timer != 0)
    {
	// Still waiting for settings
	XtRemoveTimeOut(plot->settings_timer);
	plot->settings_timer = 0;

	unlink(plot->settings_file.chars());
    }

    if (plot->settings_delay != nullptr)
    {
	plot->settings_delay->outcome = "canceled";
	delete plot->settings_delay;
	plot->settings_delay = nullptr;
    }

    plot->settings = "";

    entered = false;
}

static void CancelPlotCB(Widget, XtPointer client_data, XtPointer)
{
    static bool entered = false;
    if (entered)
	return;

    entered = true;

    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    popdown_plot_shell(plot);

    if (plot->plotter != 0)
    {
	// Terminate plotter
	plot->plotter->removeHandler(Died, PlotterNotFoundHP, client_data);
	plot->plotter->terminate();
	plot->plotter = 0;
    }

    entered = false;
}

static void DeletePlotterCB(XtPointer client_data, XtIntervalId *)
{
    Agent *plotter = (Agent *)client_data;
    delete plotter;
}

static void DeletePlotterHP(Agent *plotter, void *client_data, void *)
{
    // Defer deletion to the Xt event loop (0 ms timeout) so that DispValue is notified
    // and can clear its plotter pointer before the plotter is actually destroyed.
    XtAppAddTimeOut(XtWidgetToApplicationContext(gdb_w), 0,
		    DeletePlotterCB, XtPointer(plotter));

    plotter->removeHandler(Died, DeletePlotterHP, client_data);
    plotter->removeHandler(Died, PlotterNotFoundHP, client_data);

    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    assert(plot->plotter == 0 || plot->plotter == plotter);
    plot->plotter = 0;
    popdown_plot_shell(plot);
}

static void PlotterNotFoundHP(Agent *plotter, void *client_data, void *)
{
#if !NDEBUG
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    assert(plot->plotter == 0 || plot->plotter == plotter);
#endif

    plotter->removeHandler(Died, PlotterNotFoundHP, client_data);

    string base = app_data.plot_command;
    if (base.contains(' '))
	base = base.before(' ');

    Arg args[10];
    Cardinal arg = 0;
    MString msg = rm( capitalize(base) + " could not be started.");
    XtSetArg(args[arg], XmNmessageString, msg.xmstring()); arg++;
    Widget dialog = 
	verify(XmCreateErrorDialog(find_shell(),
				   XMST("no_plotter_dialog"), args, arg));
    if (!app_data.retro_style)
    {
        Pixmap pm = XmGetPixmap(XtScreen(dialog), (char *)"break_at", 0, 0);
        if (pm != XmUNSPECIFIED_PIXMAP)
            XtVaSetValues(dialog, XmNsymbolPixmap, pm, NULL);
    }
    XtUnmanageChild(XmMessageBoxGetChild
		    (dialog, XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(dialog, XmNhelpCallback, ImmediateHelpCB, XtPointer(0));

    Delay::register_shell(dialog);
    manage_and_raise(dialog);
}

static std::vector<PlotWindowInfo*> plot_infos;

static PlotWindowInfo *new_decoration(const string& name)
{
    PlotWindowInfo *plot = 0;

    // Check whether we can reuse an existing decoration
    for (int i = 0; i < int(plot_infos.size()); i++)
    {
	PlotWindowInfo *info = plot_infos[i];
	if (info->plotter == 0)
	{
	    // Shell is unused - use this one
	    plot = info;
	    break;
	}
    }

    if (plot == nullptr)
    {
	plot = new PlotWindowInfo;

	// Create decoration windows
	Arg args[10];
	Cardinal arg = 0;
	XtSetArg(args[arg], XmNallowShellResize, True);       arg++;
	XtSetArg(args[arg], XmNdeleteResponse, XmDO_NOTHING); arg++;

	plot->shell = verify(XtCreateWidget("plot", topLevelShellWidgetClass,
					    find_shell(), args, arg));

	AddDeleteWindowCallback(plot->shell, CancelPlotCB, XtPointer(plot));

	arg = 0;
	Widget main_window = XmCreateMainWindow(plot->shell, 
						XMST("main_window"), 
						args, arg);
	XtManageChild(main_window);

	Widget menuw = MMcreateMenuBar(main_window, "menubar", menubar);

        // add a replot button
        Widget replot_button = XmCreateCascadeButton(menuw, (char*)"replot", NULL, 0);
        XtAddCallback(replot_button, XmNactivateCallback, ReplotCB, XtPointer(plot));
        XtManageChild(replot_button);

	MMaddCallbacks(file_menu,    XtPointer(plot));
	MMaddCallbacks(simple_edit_menu);
	MMaddCallbacks(view_menu,    XtPointer(plot));
	MMaddCallbacks(plot_menu,    XtPointer(plot));
	MMaddCallbacks(scale_menu,   XtPointer(plot));
	MMaddCallbacks(contour_menu, XtPointer(plot));
	MMaddCallbacks(simple_help_menu);
	MMaddHelpCallback(menubar, ImmediateHelpCB);

        setColorMode(main_window, app_data.dark_mode, app_data.retro_style);

        // Create work window
        plot->gnuplot = XtVaCreateManagedWidget("plotArea", xmDrawingAreaWidgetClass, main_window,
                                                  XmNwidth, 640,
                                                  XmNheight, 480,
                                                  NULL);
        XtManageChild(plot->gnuplot);

	Delay::register_shell(plot->shell);
	InstallButtonTips(plot->shell);

	plot_infos.push_back(plot);
    }

    string title = DDD_NAME ": " + name;
    XtVaSetValues(plot->shell,
		  XmNtitle, title.chars(),
		  XmNiconName, title.chars(),
		  XtPointer(0));

    return plot;
}

// Remove all unused decorations from cache
void clear_plot_window_cache()
{
    for (int i = 0; i < int(plot_infos.size()); i++)
    {
	PlotWindowInfo *info = plot_infos[i];
	if (info->plotter == 0)
	{
	    // Shell is unused -- destroy it
	    XtDestroyWidget(info->shell);
	    info->shell = 0;
	}
	else
	{
	    // A running shell should be destroyed after invocation.
	    // (FIXME)
	}
    }

    plot_infos.clear();
}

// Create a new plot window
PlotAgent *new_plotter(const string& name, DispValue *source)
{
    // Create shell
    PlotWindowInfo *plot = new_decoration(name);
    if (plot == 0)
        return 0;

    popup_plot_shell(plot);
    string cmd = app_data.plot_command;
    cmd.gsub("@FONT@", make_xftfont(app_data, FixedWidthDDDFont));

    string window_name = ddd_NAME "plot";
    if (cmd.contains("@NAME@"))
        cmd.gsub("@NAME@", window_name);
    else
        cmd += " -name " + window_name;

    Window win = XtWindow(plot->gnuplot);   // Must be valid (widget realized)
    char hex[32];
    snprintf(hex, sizeof(hex), "%lx", (unsigned long)win);

    cmd.gsub("@X_ID@", hex);

    plot->source      = source;
    plot->window_name = window_name;

    // Invoke plot process
    PlotAgent *plotter = 
	new PlotAgent(XtWidgetToApplicationContext(plot->shell), cmd);

    XtAddCallback(plot->shell, XtNpopdownCallback, CancelPlotCB, XtPointer(plot));

    // Add trace handlers
    plotter->addHandler(Input,  TraceInputHP);     // Gnuplot => DDD
    plotter->addHandler(Output, TraceOutputHP);    // DDD => Gnuplot
    plotter->addHandler(Error,  TraceErrorHP, plot);     // Gnuplot Errors => DDD

    // Handle death
    plotter->addHandler(Died, PlotterNotFoundHP, (void *)plot);
    plotter->addHandler(Died, DeletePlotterHP,   (void *)plot);

    string init = app_data.plot_init_commands;

    if (!init.empty() && !init.contains('\n', -1))
	init += '\n';
   init +=
        "set mouse\n"
        "bind Button1 "
        "'print \"DDDPIX\", "
        "  MOUSE_X, MOUSE_Y, "
        "  GPVAL_X_MIN, GPVAL_X_MAX, GPVAL_Y_MIN, GPVAL_Y_MAX, "
        "  GPVAL_TERM_XMIN, GPVAL_TERM_XMAX, GPVAL_TERM_YMIN, GPVAL_TERM_YMAX, "
        "  GPVAL_TERM_XSIZE, GPVAL_TERM_YSIZE, GPVAL_TERM_SCALE"
        "'\n";

    plotter->start_with(init);
    plot->plotter = plotter;

    // Fetch plot settings
    configure_plot(plot);

    return plotter;
}



//-------------------------------------------------------------------------
// Selection stuff
//-------------------------------------------------------------------------

static void SelectPlotCB(Widget, XtPointer client_data, XtPointer)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;

    data_disp->select(plot->source);
}

static void SelectAndPrintPlotCB(Widget w, XtPointer client_data, 
				 XtPointer call_data)
{
    SelectPlotCB(w, client_data, call_data);
    PrintPlotCB(w, client_data, call_data);
}

static void SaveImageCB(Widget, XtPointer client_data, XtPointer)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    if (!plot || !plot->plotter || !plot->gnuplot)
        return;

    PixelCache *imgdata = plot->plotter->getPixelCache();
    if (!imgdata || !imgdata->valid())
    {
        post_error("No image in cache to save.", "save_image_error");
        return;
    }

    // Remember which plot window we’re saving from
    save_image_plot = plot;

    if (save_image_dialog == 0)
    {
        Arg args[10];
        int arg = 0;

        XtSetArg(args[arg], XmNpathMode, XmPATH_MODE_RELATIVE); arg++;
        XtSetArg(args[arg], XmNtextColumns, 60); arg++;           // wider
        XtSetArg(args[arg], XmNlistVisibleItemCount, 20); arg++;  // taller

        save_image_dialog =
            XmCreateFileSelectionDialog(plot->shell,
                                        XMST("save_image_dialog"),
                                        args, arg);
        Delay::register_shell(save_image_dialog);

        XtAddCallback(save_image_dialog, XmNokCallback, SaveImageOkCB, XtPointer(0));
        XtAddCallback(save_image_dialog, XmNcancelCallback, UnmanageThisCB, XtPointer(save_image_dialog));
        XtAddCallback(save_image_dialog, XmNhelpCallback, ImmediateHelpCB, XtPointer(0));

        if (!app_data.retro_style)
        {
            Widget file_list = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_LIST);
            modernize_scrollbar(file_list);

            Widget dir_list = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_DIR_LIST);
            modernize_scrollbar(dir_list);

            Widget ok = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_OK_BUTTON);
            Widget apply = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_APPLY_BUTTON);
            Widget cancel = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_CANCEL_BUTTON);
            Widget help = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_HELP_BUTTON);

            XtVaSetValues(ok, XmNshadowThickness, 1, XmNhighlightThickness, 1, NULL);
            XtVaSetValues(apply, XmNshadowThickness, 1, XmNhighlightThickness, 1, NULL);
            XtVaSetValues(cancel, XmNshadowThickness, 1, XmNhighlightThickness, 1, NULL);
            XtVaSetValues(help, XmNshadowThickness, 1, XmNhighlightThickness, 1, NULL);
        }
    }

    // Suggest a default filename each time the dialog is shown
    string def_name = plot->source->name();
    if (imgdata->channels == 1 && imgdata->data_type != PixelCache::DT_UINT8)
        def_name += ".nrrd";
    else if (imgdata->channels == 1) // Non‑uint8 grayscale -> propose NRRD
        def_name += ".pgm"; // 8‑bit grayscale -> PGM
    else
        def_name += ".ppm"; // Color -> PPM

    Widget text = XmFileSelectionBoxGetChild(save_image_dialog, XmDIALOG_TEXT);
    if (text)
        XmTextSetString(text, XMST(def_name.chars()));

    manage_and_raise(save_image_dialog);
}

static void SaveImageOkCB(Widget w, XtPointer, XtPointer call_data)
{
    // Use the common DDD helper to get the filename from the file dialog
    string filename = get_file(w, 0, call_data);
    if (filename.empty())
        return;                 // directory selected or no value yet

    XtUnmanageChild(w);

    if (filename == NO_GDB_ANSWER)
        return;                 // invalid selection

    if (!save_image_plot || !save_image_plot->plotter)
    {
        post_error("No plot to save.", "save_image_error");
        return;
    }

    PixelCache *imgdata = save_image_plot->plotter->getPixelCache();
    if (!imgdata || !imgdata->valid())
    {
        post_error("No image in cache to save.", "save_image_error");
        return;
    }

    // Decide extension / format
    bool want_nrrd = false;
    if (filename.contains('.'))
    {
        // check for ".nrrd" (lowercase)
        if (filename.contains(".nrrd"))
            want_nrrd = true;
    }
    else
    {
        // No extension: choose based on type and channels
        if (imgdata->channels == 1 && imgdata->data_type != PixelCache::DT_UINT8)
        {
            filename += ".nrrd";
            want_nrrd = true;
        }
        else if (imgdata->channels == 1)
        {
            filename += ".pgm";
        }
        else
        {
            filename += ".ppm";
        }
    }

    bool ok = false;
    if (want_nrrd)
    {
        // NRRD export currently defined for grayscale only
        ok = imgdata->saveNRRD(filename);
    }
    else
    {
        ok = imgdata->savePNM(filename);
    }

    if (!ok)
        post_error("Cannot save image in " + quote(filename), "save_image_error");
    else
        set_status("Saved image in " + quote(filename));
}

//-------------------------------------------------------------------------
// Plot again
//-------------------------------------------------------------------------

static void ReplotCB(Widget, XtPointer client_data, XtPointer)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;

    // This transfers the data once again and replots the whole thing
    plot->source->plot();
}

//-------------------------------------------------------------------------
// Command
//-------------------------------------------------------------------------

// Selection from Command widget
static void DoPlotCommandCB(Widget, XtPointer client_data, XtPointer call_data)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    XmCommandCallbackStruct *cbs = (XmCommandCallbackStruct *)call_data;

    MString xcmd(cbs->value, true);
    string cmd = xcmd.str();

    send_and_replot(plot, cmd);
}

// Apply button
static void ApplyPlotCommandCB(Widget, XtPointer client_data, XtPointer)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;

    Widget text = XmCommandGetChild(plot->command, XmDIALOG_COMMAND_TEXT);
    String cmd_s = 0;

    if (XmIsTextField(text))
	cmd_s = XmTextFieldGetString(text);
    else if (XmIsText(text))
	cmd_s = XmTextGetString(text);
    else {
        assert(0);
	::abort();
    }

    string cmd = cmd_s;
    XtFree(cmd_s);

    send_and_replot(plot, cmd);
}

static void EnableApplyCB(Widget, XtPointer client_data, XtPointer call_data)
{
    Widget apply = (Widget)client_data;
    XmCommandCallbackStruct *cbs = (XmCommandCallbackStruct *)call_data;

    set_sensitive(apply, cbs->length > 0);
}

static void PlotCommandCB(Widget, XtPointer client_data, XtPointer)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;

    if (plot->command_dialog == 0)
    {
	Arg args[10];
	Cardinal arg = 0;
	Widget dialog = 
	    verify(XmCreatePromptDialog(plot->shell,
					XMST("plot_command_dialog"),
					args, arg));
	Delay::register_shell(dialog);
	plot->command_dialog = dialog;

	Widget apply = XmSelectionBoxGetChild(dialog, XmDIALOG_APPLY_BUTTON);
	XtManageChild(apply);
    
	XtUnmanageChild(XmSelectionBoxGetChild(dialog, 
					       XmDIALOG_OK_BUTTON));
	XtUnmanageChild(XmSelectionBoxGetChild(dialog, 
					       XmDIALOG_SELECTION_LABEL));
	XtUnmanageChild(XmSelectionBoxGetChild(dialog, XmDIALOG_TEXT));

	XtAddCallback(dialog, XmNapplyCallback,
		      ApplyPlotCommandCB, XtPointer(client_data));
	XtAddCallback(dialog, XmNhelpCallback,
		      ImmediateHelpCB, XtPointer(client_data));

	arg = 0;
	Widget command = 
	    verify(XmCreateCommand(dialog, XMST("plot_command"), args, arg));
	plot->command = command;
	XtManageChild(command);

	XtAddCallback(command, XmNcommandEnteredCallback, 
		      DoPlotCommandCB, XtPointer(client_data));
	XtAddCallback(command, XmNcommandChangedCallback, 
		      EnableApplyCB, XtPointer(apply));
	set_sensitive(apply, false);
    }

    manage_and_raise(plot->command_dialog);
}

//-------------------------------------------------------------------------
// Settings
//-------------------------------------------------------------------------

static void ToggleOptionCB(Widget w, XtPointer client_data, 
			   XtPointer call_data)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    XmToggleButtonCallbackStruct *cbs = 
	(XmToggleButtonCallbackStruct *)call_data;

    string cmd;
    if (cbs->set)
	cmd = string("set ") + XtName(w);
    else
	cmd = string("unset ") + XtName(w);

    send_and_replot(plot, cmd);
}

static void ToggleLogscaleCB(Widget, XtPointer client_data, 
			     XtPointer call_data)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    XmToggleButtonCallbackStruct *cbs = 
	(XmToggleButtonCallbackStruct *)call_data;

    string cmd;
    if (cbs->set)
	cmd = "set logscale ";
    else
	cmd = "unset logscale ";

    if (plot->plotter->dimensions() >= 3)
	cmd += "z";
    else
	cmd += "y";

    send_and_replot(plot, cmd);
}

static void SetStyleCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    XmToggleButtonCallbackStruct *cbs = 
	(XmToggleButtonCallbackStruct *)call_data;

    if (cbs->set)
    {
	string style = XtName(w);
	string cmd;
	if (style.contains("3d", -1))
	{
	    cmd = "set hidden3d\n";
	    style = style.before("3d");
	}
	else
	{
	    cmd = "unset hidden3d\n";
	}
	if (style.contains("2d", -1))
	    style = style.before("2d");
	
	cmd += "set style data " + style;

	send_and_replot(plot, cmd);
    }
}

static void SetContourCB(Widget w, XtPointer client_data, XtPointer)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;

    Widget base    = XtNameToWidget(XtParent(w), "base");
    Widget surface = XtNameToWidget(XtParent(w), "surface");

    assert (base != 0 && surface != 0);

    bool base_set    = XmToggleButtonGetState(base);
    bool surface_set = XmToggleButtonGetState(surface);

    string cmd;
    if (base_set && surface_set)
	cmd = "set contour both";
    else if (base_set && !surface_set)
	cmd = "set contour base";
    else if (!base_set && surface_set)
	cmd = "set contour surface";
    else
	cmd = "set nocontour";

    send_and_replot(plot, cmd);
}



//-------------------------------------------------------------------------
// Trace communication
//-------------------------------------------------------------------------

static void trace(const char *prefix, void *call_data)
{
    DataLength* dl = (DataLength *) call_data;
    string s(dl->data, dl->length);

    bool s_ends_with_nl = false;
    if (s.length() > 0 && s[s.length() - 1] == '\n')
    {
	s_ends_with_nl = true;
	s = s.before(int(s.length() - 1));
    }

    s = quote(s);
    string nl = "\\n\"\n";
    nl += replicate(' ', strlen(prefix));
    nl += "\"";
    s.gsub("\\n", nl);

    if (s_ends_with_nl)
	s.at((int)(s.length() - 1), 0) = "\\n";

    dddlog << prefix << s << '\n';
    dddlog.flush();
}

static void TraceInputHP(Agent *, void *, void *call_data)
{
    trace("<< ", call_data);
}

static void TraceOutputHP(Agent *, void *, void *call_data)
{
    trace(">> ", call_data);
}

// The shell containing the tip label.
static Widget tip_shell               = 0;

// The tip label.
static Widget tip_label               = 0;

// The tip row; a RowColumn widget surrounding the label.
static Widget tip_row                 = 0;

// Timer to auto-popdown the tip
static XtIntervalId tip_timer         = 0;

// Refresh timer
static XtIntervalId refresh_timer         = 0;

static void TipPopdownCB(XtPointer, XtIntervalId*)
{
    if (tip_shell != 0)
        XtPopdown(tip_shell);
    tip_timer = 0;
}

void handle_pixel_pick(PlotWindowInfo *plot, int x, int y);

static bool pointer_to_data(PlotWindowInfo *plot,
                            int win_x, int win_y,
                            double &data_x, double &data_y)
{
    if (!plot || !plot->plotter || !plot->have_gpval)
        return false;

    Dimension w, h;
    XtVaGetValues(plot->gnuplot,
                  XmNwidth,  &w,
                  XmNheight, &h,
                  NULL);
    if (w <= 1 || h <= 1)
        return false;

    // window coords -> fractional screen coords (FRAC_X/FRAC_Y)
    //    FRAC origin in gnuplot is bottom-left, so flip Y.
    double frac_x = (double)win_x / (double)(w - 1);
    double frac_y = 1.0 - (double)win_y / (double)(h - 1);

    // FRAC -> gnuplot SCREEN coordinates using GPVAL_TERM_*.
    double screen_x = frac_x * plot->term_xsize / plot->term_scale;
    double screen_y = frac_y * plot->term_ysize / plot->term_scale;

    // SCREEN -> GRAPH coordinates [0,1] inside axes box
    double graph_x = (screen_x - plot->term_xmin) /
                     (plot->term_xmax - plot->term_xmin);
    double graph_y = (screen_y - plot->term_ymin) /
                     (plot->term_ymax - plot->term_ymin);

    graph_x = std::max(0.0, std::min(1.0, graph_x));
    graph_y = std::max(0.0, std::min(1.0, graph_y));

    // GRAPH -> DATA using GPVAL_X/Y_MIN/MAX
    data_x = plot->x_min + graph_x * (plot->x_max - plot->x_min);
    data_y = plot->y_min + graph_y * (plot->y_max - plot->y_min);

    return true;
}

static bool map_pointer_to_image_pixel(PlotWindowInfo *plot,
                                       int win_x, int win_y,
                                       int &ix, int &iy)
{
    if (!plot || !plot->plotter || !plot->have_gpval)
        return false;

    PixelCache *img = plot->plotter->getPixelCache();
    if (!img)
        return false;

    double data_x, data_y;
    if (!pointer_to_data(plot, win_x, win_y, data_x, data_y))
        return false;

    // Apply calibration offset from DDDPIX click
    if (plot->have_offset)
    {
        data_x += plot->x_offset;
        data_y += plot->y_offset;
    }

    ix = std::max(0, std::min(int(std::round(data_x)), img->width  - 1));
    iy = std::max(0, std::min(int(std::round(data_y)), img->height - 1));

    return true;
}

static void RefreshCB(XtPointer client_data, XtIntervalId*)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    if (!plot || !plot->plotter || !plot->gnuplot)
    {
        refresh_timer = 0;
        return;
    }

    Display *dpy = XtDisplay(plot->gnuplot);
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;

    if (!XQueryPointer(dpy, XtWindow(plot->gnuplot),
                       &root, &child,
                       &root_x, &root_y,
                       &win_x, &win_y, &mask))
    {
        refresh_timer = 0;
        return;
    }

    if (mask & Button1Mask)
    {
        int ix, iy;
        if (map_pointer_to_image_pixel(plot, win_x, win_y, ix, iy))
            handle_pixel_pick(plot, ix, iy);

        // continue polling
        refresh_timer = XtAppAddTimeOut(
            XtWidgetToApplicationContext(plot->shell),
            50,
            RefreshCB,
            plot);
    }
    else
    {
        // button released -> stop
        refresh_timer = 0;
    }
}


void handle_pixel_pick(PlotWindowInfo *plot, int x, int y)
{
    if (plot == nullptr || plot->plotter == nullptr)
        return;

    if (!plot->plotter->isImage())
        return;

    if (tip_shell == 0)
    {
        Arg args[10];
        int arg;

        Widget w = plot->shell;

        arg = 0;
        XtSetArg(args[arg], XmNallowShellResize, True);             arg++;
        XtSetArg(args[arg], XmNx, WidthOfScreen(XtScreen(w)) + 1);  arg++;
        XtSetArg(args[arg], XmNy, HeightOfScreen(XtScreen(w)) + 1); arg++;
        XtSetArg(args[arg], XmNwidth,  10);                         arg++;
        XtSetArg(args[arg], XmNheight, 10);                         arg++;
        tip_shell = verify(XmCreateMenuShell(findTheTopLevelShell(w),
                                             XMST("tipShell"), args, arg));

        arg = 0;
        XtSetArg(args[arg], XmNmarginWidth, 0);          arg++;
        XtSetArg(args[arg], XmNmarginHeight, 0);         arg++;
        XtSetArg(args[arg], XmNresizeWidth, True);       arg++;
        XtSetArg(args[arg], XmNresizeHeight, True);      arg++;
        XtSetArg(args[arg], XmNborderWidth, 0);          arg++;
        XtSetArg(args[arg], XmNshadowThickness, 0);      arg++;
        tip_row = verify(XmCreateRowColumn(tip_shell, XMST("tipRow"),
                                           args, arg));
        XtManageChild(tip_row);

        arg = 0;
        MString empty("");
        XtSetArg(args[arg], XmNlabelString, empty.xmstring()); arg++;
        XtSetArg(args[arg], XmNrecomputeSize, True);                arg++;
        XtSetArg(args[arg], XmNalignment, XmALIGNMENT_BEGINNING);   arg++;
        tip_label = XmCreateLabel(tip_row, XMST("tipLabel"), args, arg);
        XtManageChild(tip_label);

        // Realize once
        XtPopup(tip_shell, XtGrabNone);
        XtPopdown(tip_shell);
    }

    PixelCache *imgdata = plot->plotter->getPixelCache();

    if (x<0 || y<0 || x>imgdata->width || y>imgdata->height)
        return;

    char buf[64];
    snprintf(buf, sizeof(buf), "(%d, %d) = ", x, y);
    string output = buf;
    if (imgdata != nullptr)
        output += imgdata->print_pixel_value(x, y);

    MString tip = tt(output);
    XtVaSetValues(tip_label,
                  XmNlabelString, tip.xmstring(),
                  NULL);

    // Position tooltip near mouse cursor (unchanged)
    Display *dpy = XtDisplay(plot->gnuplot);
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    if (XQueryPointer(dpy, XtWindow(plot->gnuplot),
                      &root, &child,
                      &root_x, &root_y,
                      &win_x, &win_y, &mask))
    {
        XtVaSetValues(tip_shell,
                      XmNx, (Position)(root_x + 15),
                      XmNy, (Position)(root_y + 15),
                      NULL);
    }

    XtPopup(tip_shell, XtGrabNone);

    // (Re)start 0.5s auto-hide timer
    if (tip_timer != 0)
    {
        XtRemoveTimeOut(tip_timer);
        tip_timer = 0;
    }

    tip_timer = XtAppAddTimeOut(
        XtWidgetToApplicationContext(plot->shell),
        500,                 // milliseconds
        TipPopdownCB,
        nullptr);
}



static void TraceErrorHP(Agent *, void *client_data, void *call_data)
{
    PlotWindowInfo *plot = (PlotWindowInfo *)client_data;
    DataLength* dl = (DataLength *) call_data;

    // Append new chunk to our buffer
    string chunk(dl->data, dl->length);
    string &gnuplot_err_buf = plot->gnuplot_err_buf;
    gnuplot_err_buf += chunk;

    // Process complete lines
    while (gnuplot_err_buf.contains('\n'))
    {
        string line = gnuplot_err_buf.before('\n');
        gnuplot_err_buf = gnuplot_err_buf.after('\n');

        strip_space(line);
        if (line.empty())
            continue;

        // Our special line from the bind
        if (line.contains("DDDPIX", 0))
        {
            // Extract everything between DDDSTART and DDDEND
            string rest = line.after("DDDPIX");
            strip_space(rest);
            double x_data, y_data;
            double x_min, x_max, y_min, y_max;
            double txmin, txmax, tymin, tymax;
            double txsize, tysize, tscale;

            std::istringstream iss(rest.chars());
            static const std::locale c_locale("C");
            iss.imbue(c_locale);

            iss >> x_data >> y_data
                >> x_min >> x_max >> y_min >> y_max
                >> txmin >> txmax >> tymin >> tymax
                >> txsize >> tysize >> tscale;

            // Store for mapping function
            plot->x_min      = x_min;
            plot->x_max      = x_max;
            plot->y_min      = y_min;
            plot->y_max      = y_max;
            plot->term_xmin  = txmin;
            plot->term_xmax  = txmax;
            plot->term_ymin  = tymin;
            plot->term_ymax  = tymax;
            plot->term_xsize = txsize;
            plot->term_ysize = tysize;
            plot->term_scale = tscale;
            plot->have_gpval = true;

            // Get current pointer position in window coords
            Display *dpy = XtDisplay(plot->gnuplot);
            Window root, child;
            int root_x, root_y, win_x, win_y;
            unsigned int mask;
            if (XQueryPointer(dpy, XtWindow(plot->gnuplot),
                            &root, &child,
                            &root_x, &root_y,
                            &win_x, &win_y, &mask))
            {
                double est_x, est_y;
                if (pointer_to_data(plot, win_x, win_y, est_x, est_y))
                {
                    // Calibrate offsets: true (MOUSE_*) minus estimated
                    plot->x_offset   = x_data - est_x;
                    plot->y_offset   = y_data - est_y;
                    plot->have_offset = true;
                }
            }

            // Draw the initial tooltip using the *true* pixel index
            handle_pixel_pick(plot, int(std::round(x_data)), int(std::round(y_data)));

            // Start polling if not yet active
            if (refresh_timer == 0)
            {
                refresh_timer = XtAppAddTimeOut(
                    XtWidgetToApplicationContext(plot->shell),
                    50,
                    RefreshCB,
                    plot);
            }
        }
        else
        {
            // Normal gnuplot error/warning
            set_status(line);
            if (plot->command != 0)
            {
                MString xmsg = tb(line);
                XmCommandError(plot->command, xmsg.xmstring());
            }
        }
    }
}
