/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef RENDER_VIEW_H
#define RENDER_VIEW_H

#include <QtGui>
#include <QGLWidget>
#include <QtOpenGL>
#include <queue>
#include "raster.h"
#include "../h/tile.h"
#include <nlohmann/json.hpp>
#include <iomanip>

// Render view
class RENDER_VIEW : public QGLWidget { Q_OBJECT
public:
             RENDER_VIEW(QGLFormat format,
                         const char *progname,
                         QWidget *parent,
                         QStatusBar *status);
    virtual ~RENDER_VIEW();

    void set_parameter(const std::string &name, const nlohmann::json &value);

    void set_scene(const nlohmann::json &scene)
    {
        m_scene = scene;
        start_render();
    }
    const nlohmann::json &get_scene() const { return m_scene; }
    
    void save(std::ostream &os) const
    {
        os << std::setw(4) << m_scene;
    }
    void open(std::istream &is, const nlohmann::json &defs);

    // NOTE: The returned QImage references the pointer owned by this class
    QImage get_qimage() const;

public slots:
    bool start_render();
    void stop_render();

    void store_snapshot();
    void toggle_snapshot();

protected:
    bool init_shm();
    void send_tile(int tid);

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
    virtual void        timerEvent(QTimerEvent *event);

private slots:
    void    intile_event(int fd);

private:
    RASTER<uint32_t>        m_image;
    bool                    m_image_dirty = false;
    QStatusBar             *m_statusbar = nullptr;
    QLabel                 *m_statusmessage = nullptr;
    std::string             m_path;

    QGLShaderProgram       *m_program = nullptr;
    GLuint                  m_texture = 0;
    GLuint                  m_pbuffer = 0;

    // Snapshots
    RASTER<uint32_t>        m_snapshot;
    bool                    m_snapshot_dirty = false;
    bool                    m_snapshot_active = false;

    // User scene file
    nlohmann::json          m_scene;

    // Render process connection
    // {
    pid_t                m_child = 0;
    int                  m_outjson_fd = -1;
    int                  m_intile_fd = -1;
    QSocketNotifier     *m_intile_notifier = nullptr;
    int                  m_outtile_fd = -1;
    std::string          m_shm_name;
    int                  m_shm_fd = -1;
    uint                *m_shm_data = nullptr;
    // }

    // Tile queue (from the renderer)
    RES                  m_res;
    std::queue<TILE>     m_tiles;
    size_t               m_samples_complete = 0;
    double               m_start_time = 0;

    QPoint m_mousepos;
    QPoint m_offset;
    float  m_zoom = 1.0;
};

#endif
