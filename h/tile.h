/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

struct RES {
    int xres = 0;
    int yres = 0;
    int tres = 0; // Tile resolution
    int nsamples = 0; // Pixel samples
    int nthreads = 0; // Thread count

    int tile_count() const
    {
        return ((xres + tres - 1) / tres) * ((yres + tres - 1) / tres);
    }
    size_t shm_size() const
    {
        return nthreads*tres*tres*sizeof(uint32_t);
    }
};


struct TILE {
    int xoff = 0;
    int yoff = 0;
    int xsize = 0;
    int ysize = 0;
    int sidx = 0; // Sample index
    int tid = 0; // Thread index
};

