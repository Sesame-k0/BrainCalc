//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	WAD I/O functions.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

typedef struct
{
    wad_file_t wad;
    FILE *fstream;
    byte *ram_cache;
} stdc_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static wad_file_t *W_StdC_OpenFile(char *path)
{
    stdc_wad_file_t *result;
    FILE *fstream;

    fstream = fopen(path, "rb");

    if (fstream == NULL)
    {
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = M_FileLength(fstream);
    result->fstream = fstream;
    result->ram_cache = NULL;

    // Cache compact WAD/ZWAD archives (<= 8 MiB) sequentially in RAM so
    // random lump reads avoid slow host/SD seek overhead, while keeping
    // wad.mapped == NULL so ZWAD per-lump inflation still runs.
    if (result->wad.length > 0 && result->wad.length <= 8u * 1024u * 1024u)
    {
        byte *buf = (byte *)malloc(result->wad.length);
        if (buf != NULL)
        {
            fseek(fstream, 0, SEEK_SET);
            if (fread(buf, 1, result->wad.length, fstream) == result->wad.length)
            {
                result->ram_cache = buf;
                fclose(fstream);
                result->fstream = NULL;
            }
            else
            {
                free(buf);
            }
        }
    }

    return &result->wad;
}

static void W_StdC_CloseFile(wad_file_t *wad)
{
    stdc_wad_file_t *stdc_wad;

    stdc_wad = (stdc_wad_file_t *) wad;

    if (stdc_wad->fstream != NULL)
    {
        fclose(stdc_wad->fstream);
    }
    if (stdc_wad->ram_cache != NULL)
    {
        free(stdc_wad->ram_cache);
    }
    Z_Free(stdc_wad);
}

// Read data from the specified position in the file into the 
// provided buffer.  Returns the number of bytes read.

size_t W_StdC_Read(wad_file_t *wad, unsigned int offset,
                   void *buffer, size_t buffer_len)
{
    stdc_wad_file_t *stdc_wad;
    size_t result;

    stdc_wad = (stdc_wad_file_t *) wad;

    if (stdc_wad->ram_cache != NULL)
    {
        size_t avail;
        size_t n;
        if (buffer_len == 0 || offset >= stdc_wad->wad.length)
        {
            return 0;
        }
        avail = (size_t)stdc_wad->wad.length - (size_t)offset;
        n = (buffer_len < avail) ? buffer_len : avail;
        memcpy(buffer, stdc_wad->ram_cache + offset, n);
        return n;
    }

    // Jump to the specified position in the file.

    fseek(stdc_wad->fstream, offset, SEEK_SET);

    // Read into the buffer.

    result = fread(buffer, 1, buffer_len, stdc_wad->fstream);

    return result;
}


wad_file_class_t stdc_wad_file = 
{
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};


