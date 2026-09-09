// ImageCache for gnuplot interface
//
// Copyright (C) 2001-2026 Free Software Foundation, Inc.
// Written by Stefan Eickeler <eickeler@gnu.org>.
//
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

#include "pixelcache.h"

#include <float.h>
#include <stdlib.h>		// atof()

#include <map>
#include <algorithm>
#include <fstream>
#include <vector>
#include <cstdio>


// ------------------- PixelCache -------------------------------------------

bool PixelCache::read_image(string file, int xdim, int ydim, int cdim, 
                            string gdbtype, Layout layout_, StorageOrder major_)
{
    // Clear current content
    width     = 0;
    height    = 0;
    channels  = 0;
    pixel_size = 0;
    pixmap.clear();
    layout = layout_;
    storage_order  = major_;

    if (xdim <= 0 || ydim <= 0 || cdim <= 0)
        return false;

    std::ifstream is(file.chars(), std::ios::binary | std::ios::ate);
    if (!is)
        return false;

    std::streamsize total_bytes_ss = is.tellg();
    if (total_bytes_ss <= 0)
        return false;

    const size_t total_bytes = static_cast<size_t>(total_bytes_ss);
    is.seekg(0, std::ios::beg);

    const size_t num_samples = size_t(xdim) * size_t(ydim) * size_t(cdim);

    if (num_samples == 0)
        return false;

    if (total_bytes % num_samples != 0)
        return false;  // inconsistent file size

    const size_t sample_size = total_bytes / num_samples;

    // Read all bytes into pixmap
    pixmap.resize(total_bytes);
    if (!is.read(reinterpret_cast<char*>(pixmap.data()), static_cast<std::streamsize>(total_bytes)))
    {
        pixmap.clear();
        return false;
    }

    // Deduce data type from gdbtype and sample size
    bool is_float    = gdbtype.contains("float") || gdbtype.contains("double");
    bool is_unsigned = gdbtype.contains("unsigned");

    if (is_float)
    {
        if (sample_size == 4)
            data_type = DT_FLOAT32;
        else if (sample_size == 8)
            data_type = DT_FLOAT64;
        else
            return false; // unsupported float size
    }
    else
    {
        switch (sample_size)
        {
        case 1:
            data_type = is_unsigned ? DT_UINT8  : DT_INT8;
            break;
        case 2:
            data_type = is_unsigned ? DT_UINT16 : DT_INT16;
            break;
        case 4:
            data_type = is_unsigned ? DT_UINT32 : DT_INT32;
            break;
        default:
            return false; // unsupported integer size
        }
    }

    width      = xdim;
    height     = ydim;
    channels   = cdim;
    pixel_size = sample_size;

    return true;
}

bool PixelCache::write_image_interleaved(const string& filename)
{
    if (width <= 0 || height <= 0 || channels <= 0 || pixel_size == 0 || pixmap.empty())
        return false;

    const size_t num_pixels = size_t(width) * size_t(height);
    const size_t num_samples = num_pixels * size_t(channels);
    const size_t total_bytes = num_samples * pixel_size;

    if (pixmap.size() < total_bytes)
        return false;

    FILE* fp = std::fopen(filename.chars(), "wb");
    if (!fp)
        return false;

    if (layout==L_PLANAR && channels==3)
    {
        uint8_t* red   = pixmap.data();
        uint8_t* green = red   + num_pixels * pixel_size;
        uint8_t* blue  = green + num_pixels * pixel_size;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                std::fwrite(red,   pixel_size, 1, fp);
                red   += pixel_size;
                std::fwrite(green, pixel_size, 1, fp);
                green += pixel_size;
                std::fwrite(blue,  pixel_size, 1, fp);
                blue  += pixel_size;
            }
        }
    }
    else if (storage_order==COL_MAJOR && channels==1)
    {
        //write transpose
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                uint8_t* pixel = (y + (x * height)) * pixel_size + pixmap.data();
                std::fwrite(pixel,   pixel_size, 1, fp);
            }
        }
    }
    else
    {
        std::fwrite(pixmap.data(), total_bytes, 1, fp);
    }

    std::fclose(fp);
    return true;
}

bool PixelCache::savePNM(const string& filename)
{
    if (width <= 0 || height <= 0 || channels <= 0 || pixel_size == 0 || pixmap.empty())
        return false;

    if (data_type != DT_UINT8 || (channels!=1 && channels!=3))
        return false;

    FILE *fp = fopen(filename.chars(), "wb");
    if (fp==nullptr)
        return false;

    if (channels == 1)
    {
        if (fprintf(fp,"P5\n")<0)
        {
            fclose(fp);
            return false;
        }

        fprintf(fp,"%d %d\n255\n", width, height);
    }
    else
    {
        if (fprintf(fp,"P6\n")<0)
        {
            fclose(fp);
            return false;
        }

        fprintf(fp,"%d %d\n255\n", width, height);
    }

    for (int y=0; y<height; y++)
    {
        for (int x=0; x<width; x++)
        {
            for (int c=0; c<channels; c++)
            {
                if (fwrite(pixelat(x, y, c), sizeof(char), 1, fp) == 0)
                {
                    fclose(fp);
                    return false;
                }
            }
        }
    }


    fclose(fp);
    return true;
}

bool PixelCache::saveNRRD(const string& filename)
{
    // Only grayscale images (channels == 1)
    if (width <= 0 || height <= 0 || channels != 1 || pixel_size == 0 || pixmap.empty())
        return false;

    const size_t total_bytes = size_t(width) * size_t(height) * size_t(pixel_size);

    if (pixmap.size() < total_bytes)
        return false;

    FILE *fp = fopen(filename.chars(), "wb");
    if (fp == 0)
        return false;

    const char *typeStr = 0;
    switch (data_type)
    {
    case DT_UINT8:   typeStr = "uchar";  break;
    case DT_INT8:    typeStr = "char";   break;
    case DT_UINT16:  typeStr = "ushort"; break;
    case DT_INT16:   typeStr = "short";  break;
    case DT_UINT32:  typeStr = "uint";   break;
    case DT_INT32:   typeStr = "int";    break;
    case DT_FLOAT32: typeStr = "float";  break;
    case DT_FLOAT64: typeStr = "double"; break;
    default:
        fclose(fp);
        return false;
    }

    if (fprintf(fp, "NRRD0005\n") < 0 ||
        fprintf(fp, "type: %s\n", typeStr) < 0 ||
        fprintf(fp, "dimension: 2\n") < 0 ||
        fprintf(fp, "sizes: %d %d\n", width, height) < 0 ||
        fprintf(fp, "encoding: raw\n") < 0)
    {
        fclose(fp);
        return false;
    }

    // Endianness for multi‑byte types
    if (pixel_size > 1)
    {
        uint16_t x = 1;
        const char *endianStr = (*(uint8_t *)&x == 1) ? "little" : "big";
        if (fprintf(fp, "endian: %s\n", endianStr) < 0)
        {
            fclose(fp);
            return false;
        }
    }

    if (fprintf(fp, "\n") < 0)  // blank line before raw data
    {
        fclose(fp);
        return false;
    }

    if (fwrite(pixmap.data(), total_bytes, 1, fp) != 1)
    {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}

