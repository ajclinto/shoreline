/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef RASTER_H
#define RASTER_H

#include <stdlib.h>
#include <string.h>

template <typename T>
class RASTER {
public:
    void resize(int width, int height)
    {
        m_width = width;
        m_height = height;
        m_data.resize(width*height);
    }

    int width() const       { return m_width; }
    int height() const      { return m_height; }
    size_t  bytes() const   { return m_width*m_height*sizeof(T); }
    const T *data() const   { return m_data.data(); }
    T *data()               { return m_data.data(); }

    T *get_scan(int y)
    {
        return &m_data[y*m_width];
    }

private:
	std::vector<T>	m_data;
	int				m_width;
	int				m_height;
};

#endif
