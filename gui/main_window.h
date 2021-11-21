/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QtGui>
#include <QGLWidget>
#include <QtOpenGL>

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

public slots:
    void    toolbar(bool value);

private:
    QMenu             *m_file_menu;
    QAction           *m_quit;
    QToolBar          *m_toolbar;
    RENDER_VIEW       *m_renderview;
};

#endif
