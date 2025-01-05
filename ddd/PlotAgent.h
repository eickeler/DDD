// $Id$ -*- C++ -*-
// Gnuplot interface

// Copyright (C) 1998 Technische Universitaet Braunschweig, Germany.
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

#ifndef _DDD_PlotAgent_h
#define _DDD_PlotAgent_h

#include "agent/LiterateA.h"
#include "assert.h"
#include "base/PrintGC.h"
#include "agent/ChunkQueue.h"

#include <fstream>
#include <vector>
#include <algorithm>

// Event types
const unsigned Plot = LiterateAgent_NTypes;   // Plot data received

const unsigned PlotAgent_NTypes = Plot + 1;   // number of events

struct PlotElement
{
    enum {DATA_1D, DATA_2D, DATA_3D, IMAGE, RGBIMAGE, RGBAIMAGE} plottype = DATA_2D;
    string file;		// allocated temporary file
    string title;		// Title currently plotted
    string value;		// Scalar
    bool binary = false;        // true for binary files
    string gdbtype;             // type of the variable as reported by GDB
    string xdim;                // x dimension of array
    string ydim;                // y dimension of array
};

class PlotAgent: public LiterateAgent {

public:
    DECLARE_TYPE_INFO

private:

    std::vector<PlotElement> elements;  // Data for the elements of the plot command
    std::ofstream plot_os;		// Stream used for adding data

    string init_commands;	// Initialization commands
    bool need_reset;		// Reset with next plot

    bool getting_plot_data;	// True if getting plot data

protected:
    void reset();

    virtual void dispatch(int type, const char *data, int length);

    string getGnuplotType(string gdbtype);

public:
    static string plot_2d_settings;
    static string plot_3d_settings;

    // Constructor for Agent users
    PlotAgent(XtAppContext app_context, const string& pth,
	      unsigned nTypes = PlotAgent_NTypes)
	: LiterateAgent(app_context, pth, nTypes),
	  plot_os(), init_commands(""),
	  need_reset(false), getting_plot_data(false)
    {
	reset();
    }

    // Start and initialize
    void start_with(const string& init);

    // Kill
    void abort();

    // Start plotting new data with TITLE in NDIM dimensions
    PlotElement &start_plot(const string& title);
    void open_stream(const PlotElement &emdata);

    // Add plot point
    void add_point(int x, const string& v);
    void add_point(double x, const string& v);
    void add_point(int x, int y, const string& v);
    void add_point(double x, double y, const string& v);

    // Add a break
    void add_break();

    // End plot
    void close_stream();

    // Flush accumulated data
    int flush();

    // Return number of dimensions
    int dimensions() const
    {
        if (std::any_of(elements.begin(), elements.end(), [&](const PlotElement &elem) { return elem.plottype == PlotElement::DATA_3D; }))
            return 3;
        return 2;
    }

    // Print plot to FILENAME
    void print(const string& filename, 
	       const PrintGC& gc = PostScriptPrintGC());

    // Show plot state
    void set_state(const string& state);
};

#endif // _DDD_PlotAgent_h
// DON'T ADD ANYTHING BEHIND THIS #endif
