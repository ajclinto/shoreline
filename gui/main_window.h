/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QtGui>
#include <QGLWidget>
#include <QtOpenGL>
#include <nlohmann/json.hpp>

class RENDER_VIEW;

class MAIN_WINDOW : public QMainWindow { Q_OBJECT
public:
             MAIN_WINDOW(const char *progname);
    virtual ~MAIN_WINDOW();

    QSize                sizeHint() const;

private:
    QActionGroup        *createActionGroup(QMenu *menu,
                                           const char *names[],
                                           QAction *actions[],
                                           int count,
                                           int def_action);

private:
    QMenu             *m_file_menu;
    QAction           *m_quit;

    // Central widget
    RENDER_VIEW       *m_renderview;

    // Top toolbar
    QToolBar          *m_toolbar;
    QAction           *m_render_action;
    QAction           *m_stop_action;

    // Right dock
    QDockWidget       *m_dock;
    QVBoxLayout       *m_dock_layout;
    QWidget           *m_params;
    nlohmann::json     m_json_ui;
};

#endif
