// ImageCache for gnuplot interface
//
// Copyright (C) 2001-2026 Free Software Foundation, Inc.
// Written by Stefan Eickeler <eickeler@gnu.org>.
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

#ifndef _DDD_IMAGECACHE_h
#define _DDD_IMAGECACHE_h

#include "assert.h"
#include "base/strclass.h"

#include <fstream>
#include <vector>
#include <algorithm>

#include <stdint.h>


struct PixelCache
{
    enum DataType {
        DT_UINT8,
        DT_INT8,
        DT_UINT16,
        DT_INT16,
        DT_UINT32,
        DT_INT32,
        DT_FLOAT32,
        DT_FLOAT64
    };

    enum Layout {
        L_INTERLEAVED,   // pixel: [c0, c1, c2, ...]
        L_PLANAR         // plane0 all, plane1 all, ...
    };

    enum StorageOrder {
        ROW_MAJOR,          // normal row major format
        COL_MAJOR        // fortran style column major format
    };

    DataType data_type = DT_UINT8;
    Layout   layout = L_INTERLEAVED;
    StorageOrder storage_order = ROW_MAJOR;

    int      width = 0;
    int      height = 0;
    int      channels = 0;      // e.g. 1 or 3
    size_t   pixel_size = 0;    // bytes per channel sample

    std::vector<uint8_t> pixmap;  // raw bytes, size = width*height*channels*pixel_size

    PixelCache() {}

    bool valid() const
    {
        return width > 0 && height > 0 && channels > 0 && !pixmap.empty();
    }

    bool read_image(string file, int xdim, int ydim, int cdim, string gdbtype, Layout layout, StorageOrder storage_order=ROW_MAJOR);
    bool write_image_interleaved(const string& filename);
    bool savePNM(const string& filename);
    bool saveNRRD(const string& filename);

    void *pixelat(int x, int y, int c = 0)
    {
        if (storage_order==ROW_MAJOR)
        {
            if (layout==L_PLANAR)
                return &pixmap[((c * height + y) * width + x) * pixel_size];
            else
                return &pixmap[((y * width + x) * channels + c) * pixel_size];
        }
        else
        {
            if (layout==L_PLANAR)
                return &pixmap[((c * width + x) * height + y) * pixel_size];
            else
                return &pixmap[((x * height + y) * channels + c) * pixel_size];
        }

    }

    string print_pixel_value(int x, int y)
    {
        if (x<0 || x>width || y<0 || y>height)
            return "";

        string result = "";
        char buf[64];
        for (int c=0; c<channels; c++)
        {
            switch (data_type)
            {
                case DT_UINT8:
                    snprintf(buf, sizeof(buf), "%3d", *(uint8_t*)pixelat(x, y, c));
                    break;
                case DT_INT8:
                    snprintf(buf, sizeof(buf), "%3d", *(int8_t*)pixelat(x, y, c));
                    break;
                case DT_UINT16:
                    snprintf(buf, sizeof(buf), "%d", *(uint16_t*)pixelat(x, y, c));
                    break;
                case DT_INT16:
                    snprintf(buf, sizeof(buf), "%d", *(int16_t*)pixelat(x, y, c));
                    break;
                case DT_UINT32:
                    snprintf(buf, sizeof(buf), "%d", *(uint32_t*)pixelat(x, y, c));
                    break;
                case DT_INT32:
                    snprintf(buf, sizeof(buf), "%d", *(int32_t*)pixelat(x, y, c));
                    break;
                case DT_FLOAT32:
                    snprintf(buf, sizeof(buf), "%f", *(float*)pixelat(x, y, c));
                    break;
                case DT_FLOAT64:
                    snprintf(buf, sizeof(buf), "%lf", *(double*)pixelat(x, y, c));
                    break;
            }

            result += buf;
            if (c<channels-1)
                result += ", ";
        }
        return result;
    }
};


#endif // _DDD_IMAGECACHE_h
// DON'T ADD ANYTHING BEHIND THIS #endif
