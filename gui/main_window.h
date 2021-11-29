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
    QAction           *m_save;
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

class COLOR_WIDGET : public QPushButton { Q_OBJECT
public:
    COLOR_WIDGET(const QColor &color, QWidget* parent)
        : QPushButton(parent)
    {
        setColor(color);
        connect( this, SIGNAL(clicked()), this, SLOT(changeColor()) );
    }

signals:
    void valueChanged(const QColor &color);

public slots:
    void changeColor()
    {
        QColorDialog *dialog = new QColorDialog(m_color, parentWidget());
        connect(dialog, &QColorDialog::currentColorChanged, this, &COLOR_WIDGET::setColor);
        connect(dialog, &QColorDialog::colorSelected, this, &COLOR_WIDGET::setColor);
        dialog->show();
    }
    void setColor(const QColor& color)
    {
        if (color != m_color)
        {
            m_color = color;
            setStyleSheet("background-color: " + m_color.name() + "; border:none;");
            valueChanged(m_color);
        }
    }

private:
    QColor m_color;
};

#endif
