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
        if (width != m_width || height != m_height)
        {
            m_width = width;
            m_height = height;
            m_data.assign(width*height, T());
        }
    }

    int width() const       { return m_width; }
    int height() const      { return m_height; }
    size_t bytes() const    { return m_data.size()*sizeof(T); }
    bool empty() const      { return m_data.empty(); }
    const T *data() const   { return m_data.data(); }
    T *data()               { return m_data.data(); }

    T *get_scan(int y)
    {
        return &m_data[y*m_width];
    }

private:
	std::vector<T>	m_data;
	int				m_width = 0;
	int				m_height = 0;
};

#endif
