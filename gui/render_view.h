/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef RENDER_VIEW_H
#define RENDER_VIEW_H

#include <QtGui>
#include <QGLWidget>
#include <QtOpenGL>
#include "raster.h"
#include "../h/tile.h"

// Render view
class RENDER_VIEW : public QGLWidget { Q_OBJECT
public:
             RENDER_VIEW(QGLFormat format,
                         const char *progname,
                         QWidget *parent,
                         QStatusBar *status);
    virtual ~RENDER_VIEW();

protected:
    bool start_render();
    bool init_shared_memory();

    virtual void        initializeGL();
    virtual void        resizeGL(int width, int height);
    virtual void        paintGL();

    virtual bool        event(QEvent *event);

    virtual void        resizeEvent(QResizeEvent *event);

    virtual void        mousePressEvent(QMouseEvent *event);
    virtual void        mouseMoveEvent(QMouseEvent *event);
    virtual void        mouseReleaseEvent(QMouseEvent *event);
    virtual void        wheelEvent(QWheelEvent *event);
    virtual void        keyPressEvent(QKeyEvent *event);

private slots:
    void    socket_event(int fd);

private:
    RASTER<uint32_t>        m_image;
    bool                    m_image_dirty = true;
    QStatusBar             *m_statusbar;
    QLabel                 *m_statusmessage;
    std::string             m_path;

    QGLShaderProgram       *m_program;
    GLuint                  m_texture;
    GLuint                  m_pbuffer;

    // Render process data
	pid_t			 m_child;
	int				 m_inpipe_fd;
	FILE			*m_inpipe;
    QSocketNotifier *m_inpipe_notifier;
	int				 m_outpipe_fd;
	FILE			*m_outpipe;

	std::vector<TILE>	m_tiles;
	int					m_current_tile = 0;

	std::string		 m_shared_name;
	uint            *m_shared_data;

    QPoint m_mousepos;
    QPoint m_offset;
    float  m_zoom = 1.0;
};

#endif
