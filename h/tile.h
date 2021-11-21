/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

struct RES {
    int xres = 0;
    int yres = 0;
    int tres = 0; // tile res

    int tile_count() const
    {
        return ((xres + tres - 1) / tres) * ((yres + tres - 1) / tres);
    }
};


struct TILE {
    int xoff = 0;
    int yoff = 0;
    int xsize = 0;
    int ysize = 0;
};

static const int ASKTILE = 13;
static const int TILEDATA = 14;
