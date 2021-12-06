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
class PARAMETER;

class MAIN_WINDOW : public QMainWindow { Q_OBJECT
public:
             MAIN_WINDOW(const char *progname, const char *filename);
    virtual ~MAIN_WINDOW();

    QSize                sizeHint() const;

private slots:
    void reset();
    void save();
    void save_as();
    void open();
    void open_file(const QString &fname);

    void save_image_as();

private:
    QActionGroup        *createActionGroup(QMenu *menu,
                                           const char *names[],
                                           QAction *actions[],
                                           int count,
                                           int def_action);

    void update_parameters();

private:
    QMenu             *m_file_menu;
    QAction           *m_new;
    QAction           *m_open;
    QAction           *m_save;
    QAction           *m_save_as;
    QAction           *m_save_image_as;
    QAction           *m_quit;

    QString            m_open_file;

    // Central widget
    RENDER_VIEW       *m_renderview;

    // Top toolbar
    QToolBar          *m_toolbar;
    QAction           *m_render_action;
    QAction           *m_stop_action;

    // Right dock
    QDockWidget                         *m_dock;
    QVBoxLayout                         *m_dock_layout;
    std::map<std::string, PARAMETER *>   m_parameters;
    nlohmann::json                       m_json_ui;
    nlohmann::json                       m_defaults;
};

#endif
