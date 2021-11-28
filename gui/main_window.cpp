/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "main_window.h"
#include "render_view.h"
#include <QtGui>
#include <fstream>
#include <iostream>

static const QSize        theDefaultSize(1024, 768);

MAIN_WINDOW::MAIN_WINDOW(const char *progname)
{
    setStatusBar(statusBar());

    // The constructor for RENDER_VIEW will handle falling back to OpenGL 3.0
    // if it finds that 3.3 isn't supported
    QGLFormat format(QGL::NoDepthBuffer);

    QGLFormat::OpenGLVersionFlags flags = QGLFormat::openGLVersionFlags();
    if (!(flags & QGLFormat::OpenGL_Version_3_3))
    {
        fprintf(stderr, "Detected old OpenGL version, trying to force it to 3.3\n");

        // Try to force the version to 3.3. This seems to correct problems with
        // mesa, where GL 3.3 is only supported in core profile.
        format.setVersion(3,3);
    }

    //format.setProfile(QGLFormat::CompatibilityProfile);
    //format.setProfile(QGLFormat::CoreProfile); // Requires >=Qt-4.8.0
    //format.setSampleBuffers(true);

    m_renderview = new RENDER_VIEW(format, progname, this, statusBar());

    m_dock = new QDockWidget(this);
    m_dock->setWindowTitle("Parameters");
    m_dock->setFixedWidth(150);
    addDockWidget(Qt::RightDockWidgetArea, m_dock);

    {
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("../renderer/slrender --dump_ui", "r"), pclose);
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        std::string ss;
        std::array<char, 256> buf;
        while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr)
        {
            ss += buf.data();
        }
        m_json_ui = nlohmann::json::parse(ss);
    }

    auto layout = new QGridLayout;
    m_params = new QWidget;
    m_params->setLayout(layout);
    m_dock->setWidget(m_params);
    int row = 0;
    for (const auto &json_p : m_json_ui)
    {
        const auto &name = json_p["name"];
        layout->addWidget(new QLabel(QString(name.get<std::string>().c_str()), m_params), row, 0);

        if (json_p["type"] == "float")
        {
            auto *p = new QDoubleSpinBox(m_params);
            layout->addWidget(p, row, 1);
            p->setMinimum(json_p["min"]);
            p->setMaximum(json_p["max"]);

            double def = json_p["default"];
            m_renderview->set_parameter(name, def);
            p->setValue(def);

            connect(p, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
                    [&](double value) { m_renderview->set_parameter(name, value); }
                    );
        }
        else
        {
            auto *p = new QSlider(Qt::Horizontal, m_params);
            layout->addWidget(p, row, 1);
            p->setMinimum(json_p["min"]);
            p->setMaximum(json_p["max"]);

            int def = json_p["default"];
            m_renderview->set_parameter(name, def);
            p->setValue(def);

            connect(p, &QSlider::valueChanged, this,
                    [&](int value) { m_renderview->set_parameter(name, value); }
                    );
        }

        row++;
    }

    m_quit = new QAction(tr("&Quit"), this);

    connect(m_quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_file_menu = menuBar()->addMenu(tr("&File"));
    m_file_menu->addSeparator();
    m_file_menu->addAction(m_quit);

    setWindowTitle("Shoreline Renderer");

    setCentralWidget(m_renderview);

    m_toolbar = new QToolBar("Tools");
    m_toolbar->setAllowedAreas(Qt::TopToolBarArea);
    m_toolbar->setFixedHeight(30);

    m_render_action = m_toolbar->addAction("Render");
    m_stop_action = m_toolbar->addAction("Stop");
    auto snapshot_action = m_toolbar->addAction("Take Snapshot");
    auto toggle_action = m_toolbar->addAction("Toggle Snapshot");
    connect(m_render_action, &QAction::triggered, m_renderview, &RENDER_VIEW::start_render);
    connect(m_stop_action, &QAction::triggered, m_renderview, &RENDER_VIEW::stop_render);
    connect(snapshot_action, &QAction::triggered, m_renderview, &RENDER_VIEW::store_snapshot);
    connect(toggle_action, &QAction::triggered, m_renderview, &RENDER_VIEW::toggle_snapshot);

    addToolBar(Qt::TopToolBarArea, m_toolbar);

    m_renderview->start_render();
}

MAIN_WINDOW::~MAIN_WINDOW()
{
}

QSize
MAIN_WINDOW::sizeHint() const
{
    return theDefaultSize;
}

QActionGroup *
MAIN_WINDOW::createActionGroup(
        QMenu *menu,
        const char *names[],
        QAction *actions[],
        int count,
        int def_action)
{
    QActionGroup *group = new QActionGroup(this);
    for (int i = 0; i < count; i++)
    {
        actions[i] = new QAction(tr(names[i]), group);
        actions[i]->setCheckable(true);
        menu->addAction(actions[i]);
    }
    actions[def_action]->setChecked(true);
    return group;
}

