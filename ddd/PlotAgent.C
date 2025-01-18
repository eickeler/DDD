// $Id$ -*- C++ -*-
// Gnuplot interface

// Copyright (C) 1998 Technische Universitaet Braunschweig, Germany.
// Copyright (C) 2001 Universitaet Passau, Germany.
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

char PlotAgent_rcsid[] = 
    "$Id$";

#include "PlotAgent.h"
#include "base/cook.h"
#include "version.h"
#include "tempfile.h"

#include <float.h>
#include <stdlib.h>		// atof()

#include <map>
#include <algorithm>


DEFINE_TYPE_INFO_1(PlotAgent, LiterateAgent)

string PlotAgent::plot_2d_settings = "";
string PlotAgent::plot_3d_settings = "";

// Start and initialize
void PlotAgent::start_with(const string& init)
{
    LiterateAgent::start();
    write(init.chars(), init.length());
    init_commands = init;
}

// Reset for next plot
void PlotAgent::reset()
{
    // Clear storage
    elements.clear();

    need_reset = false;
}

// Start a new plot
PlotElement &PlotAgent::start_plot(const string& title)
{
    if (need_reset)
	reset();

    PlotElement st;
    st.title = title;
    st.file = tempfile();
    elements.push_back(st);

    return elements.back();
}

void PlotAgent::open_stream(const PlotElement &emdata)
{
    // Open plot stream
    plot_os.open(emdata.file.chars());

    // Issue initial line
    if (emdata.plottype==PlotElement::DATA_3D)
        plot_os << "# " DDD_NAME ": " << emdata.title << "\n"
                    << "# Use `splot' to plot this data.\n"
                    << "# X\tY\tVALUE\n";
    else
        plot_os << "# " DDD_NAME ": " << emdata.title << "\n"
            << "# Use `plot' to plot this data.\n"
            << "# X\tVALUE\n";
    return;
}

// End a new plot
void PlotAgent::close_stream()
{
    // Close plot stream
    plot_os.close();
}

const static std::map<string, string> gdb2gnuplot = {
    {string("char"), string("char")},
    {string("unsigned char"), string("uchar")},
    {string("short"), string("short")},
    {string("unsigned short"), string("ushort")},
    {string("int"), string("int")},
    {string("unsigned int"), string("uint")},
    {string("long"), string("long")},
    {string("unsigned long"), string("ulong")},
    {string("float"), string("float")},
    {string("double"), string("double")}
};


string PlotAgent::getGnuplotType(string gdbtype)
{
    auto search = gdb2gnuplot.find(gdbtype);
    if (search == gdb2gnuplot.end())
    {
//         printf("unknown type \"%s\"\n", gdbtype.chars());
        return "";
    }

    return search->second;
}

// Flush it all
int PlotAgent::flush()
{
    if (elements.size() == 0)
        return -1;  // No data - ignore

    // Issue plot command
    string cmd;
    if (is_any_of_elements(PlotElement::IMAGE) || is_any_of_elements(PlotElement::RGBIMAGE) || is_any_of_elements(PlotElement::BGRIMAGE))
    {
        cmd += "set yrange reverse\n";
        cmd += "set size ratio -1; set view equal xy\n";
        if (is_any_of_elements(PlotElement::IMAGE))
            cmd += "set palette rgbformulae 21,22,23\n";
    }

    int ndim = dimensions();
    if (ndim==3)
    {
        if (!plot_3d_settings.empty())
            cmd += plot_3d_settings + "\n";

        cmd += "splot ";
    }
    else
    {
        if (!plot_2d_settings.empty())
            cmd += plot_2d_settings + "\n";

        cmd += "plot ";
    }

    // Issue functions
    for (int i = 0; i < int(elements.size()); i++)
    {
        PlotElement &elem = elements[i];

        string gnuplottype = getGnuplotType(elem.gdbtype);
        if (elements[i].binary && gnuplottype=="")
            continue;  // unknowm type

        if (i > 0)
            cmd += ", ";

        // value or filename
        if (!elem.value.empty())
            cmd += elem.value;
        else
            cmd += quote(elem.file) + " "; // Plot a file


        if (elements[i].binary)
        {
            cmd += "binary format='%" + getGnuplotType(elem.gdbtype) + "' ";

            if (!elem.xdim.empty())
            {
                cmd += "array=(" + elem.xdim;
                if (!elem.ydim.empty())
                    cmd += "," + elem.ydim;
                cmd += ") ";
            }

            switch (elem.plottype)
            {
                case PlotElement::DATA_2D:
                    if (ndim==3)
                        cmd += "using 0:(0):1 "; // convert 2D data to 3D
                    break;
                case PlotElement::IMAGE:
                    if (gnuplottype=="uchar")
                        cmd += " using 1:1:1 with rgbimage ";
                    else
                        cmd += " with image";
                    break;
                case PlotElement::RGBIMAGE:
                        cmd += " with rgbimage ";
                    break;
                case PlotElement::BGRIMAGE:
                        cmd += "using 3:2:1 with rgbimage ";
                    break;
                default:
                    break;
            }
        }
        else
        {
            switch (elem.plottype)
            {
                case PlotElement::DATA_2D:
                    if (ndim==3)
                        cmd += "u 1:(0):2 "; // convert 2D data to 3D
                    else
                        cmd += "u 1:2 ";
                    break;
                default:
                    break;
            }
        }

        // Add title
        cmd += " title " + quote(elem.title);
    }
    cmd += "\n";
    // That's all, folks!
    write(cmd.chars(), cmd.length());

    need_reset = true;

    return LiterateAgent::flush();
}

// Done
void PlotAgent::abort()
{
    // Unlink temporary files
    for (int i = 0; i < int(elements.size()); i++)
	unlink(elements[i].file.chars());

    // We're done
    LiterateAgent::abort();
}


// Add plot point
void PlotAgent::add_point(int x, const string& v)
{
    plot_os << x << '\t' << v << '\n';
}

void PlotAgent::add_point(double x, const string& v)
{
    plot_os << x << '\t' << v << '\n';
}

void PlotAgent::add_point(int x, int y, const string& v)
{
    plot_os << x << '\t' << y << '\t' << v << '\n';
}

void PlotAgent::add_point(double x, double y, const string& v)
{
    plot_os << x << '\t' << y << '\t' << v << '\n';
}

void PlotAgent::add_break()
{
    plot_os << '\n';
}

// Show whether plot is active
void PlotAgent::set_state(const string& state)
{
    string title;
    if (!state.empty())
	title = " " + quote('(' + state + ')');

    const string c = "set title" + title + "\nreplot\n";
    write(c.chars(), c.length());
}

// Print plot to FILENAME
void PlotAgent::print(const string& filename, const PrintGC& gc)
{
    std::ostringstream cmd;

    if (gc.isFig())
    {
	cmd << "set term fig\n";
    }
    else if (gc.isPostScript())
    {
	const PostScriptPrintGC& ps = const_ref_cast(PostScriptPrintGC, gc);

	cmd << "set term postscript";

	switch (ps.orientation)
	{
	case PostScriptPrintGC::PORTRAIT:
	{
	    // Portrait plotting doesn't make too much sense, so we
	    // use the more useful EPS plotting instead.
	    cmd << " eps";
	    break;
	}

	case PostScriptPrintGC::LANDSCAPE:
	{
	    cmd << " landscape";
	    break;
	}
	}

	if (ps.color)
	{
	    cmd << " color";
	}
	else
	{
	    cmd << " monochrome";
	}

	cmd << "\n";


	switch (ps.orientation)
	{
	case PostScriptPrintGC::PORTRAIT:
	{
	    // Use the default size for EPS.
	    cmd << "set size\n";
	    break;
	}
	case PostScriptPrintGC::LANDSCAPE:
	{
	    // Postscript defaults are: landscape 10" wide and 7" high.
	    // Our `vsize' and `hsize' members assume portrait, so they
	    // are just reversed.
	    const int default_hsize =  7 * 72; // Default width in 1/72"
	    const int default_vsize = 10 * 72; // Default height in 1/72"

	    // Leave 1" extra vertical space
	    const int hoffset = 0;
	    const int voffset = 1 * 72;

	    // Set size
	    double xscale = double(ps.hsize - hoffset) / default_hsize;
	    double yscale = double(ps.vsize - voffset) / default_vsize;

	    cmd << "set size " << xscale << ", " << yscale << "\n";
	}
	}
    }

    cmd << "set output " << quote(filename) << "\n"
	<< "replot\n"
	<< "set output\n"
	<< "set size\n"
	<< init_commands << "\n"
	<< "replot\n";

    string c(cmd);
    write(c.chars(), c.length());
}
