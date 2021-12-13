/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "main_window.h"
#include "render_view.h"
#include "parameter.h"
#include <QtGui>
#include <fstream>
#include <iostream>

static const QSize        theDefaultSize(1200, 900);

MAIN_WINDOW::MAIN_WINDOW(const char *progname, const char *filename)
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
    auto params = new QWidget;
    params->setLayout(layout);
    for (const auto &json_p : m_json_ui)
    {
        PARAMETER *p = PARAMETER::create_parameter(json_p, layout, params);
        const auto &name = json_p["name"];
        p->setValue(json_p["default"]);
        m_defaults[(std::string)name] = json_p["default"];
        m_parameters[name] = p;

        connect(p, &PARAMETER::valueChanged,
                this, [this,name](const nlohmann::json &value) { m_renderview->set_parameter(name, value); });
    }

    // Compacts rows
    layout->setRowStretch(layout->rowCount(), 1);

    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(params);
    m_dock->setWidget(scroll);

    m_new = new QAction(tr("&New"), this);
    m_new->setShortcut(QKeySequence::New);
    m_open = new QAction(tr("&Open"), this);
    m_open->setShortcut(QKeySequence::Open);
    m_save = new QAction(tr("&Save"), this);
    m_save->setShortcut(QKeySequence::Save);
    m_save_as = new QAction(tr("&Save As..."), this);
    m_save_as->setShortcut(QKeySequence::SaveAs);
    m_save_image_as = new QAction(tr("&Save Image As..."), this);
    m_quit = new QAction(tr("&Quit"), this);
    m_quit->setShortcut(QKeySequence::Quit);

    connect(m_new, &QAction::triggered, this, &MAIN_WINDOW::reset);
    connect(m_open, &QAction::triggered, this, &MAIN_WINDOW::open);
    connect(m_save, &QAction::triggered, this, &MAIN_WINDOW::save);
    connect(m_save_as, &QAction::triggered, this, &MAIN_WINDOW::save_as);
    connect(m_save_image_as, &QAction::triggered, this, &MAIN_WINDOW::save_image_as);
    connect(m_quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_file_menu = menuBar()->addMenu(tr("&File"));
    m_file_menu->addAction(m_new);
    m_file_menu->addAction(m_open);
    m_file_menu->addAction(m_save);
    m_file_menu->addAction(m_save_as);
    m_file_menu->addSeparator();
    m_file_menu->addAction(m_save_image_as);
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

    if (filename)
    {
        open_file(QString(filename));
    }
    else
    {
        m_renderview->set_scene(m_defaults);
    }
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

void MAIN_WINDOW::reset()
{
    m_renderview->set_scene(m_defaults);
    update_parameters();
}

void MAIN_WINDOW::save()
{
    if (m_open_file.isEmpty())
    {
        save_as();
        return;
    }

    std::ofstream out(QFile::encodeName(m_open_file).data());
    if (!out)
    {
        QMessageBox::information(this, tr("Unable to save file"), m_open_file);
        m_open_file = QString();
        return;
    }
    m_renderview->save(out);
}

void MAIN_WINDOW::save_as()
{
    auto fname = QFileDialog::getSaveFileName(
        this,
        tr("Save Scene"), "",
        tr(".json scene (*.json);;All Files (*)"));
    if (fname.isEmpty()) return;

    std::ofstream out(QFile::encodeName(fname).data());
    if (!out)
    {
        QMessageBox::information(this, tr("Unable to save file"), fname);
        return;
    }
    m_renderview->save(out);
    m_open_file = fname;
}

void MAIN_WINDOW::open()
{
    auto fname = QFileDialog::getOpenFileName(
        this,
        tr("Open Scene"), "",
        tr(".json scene (*.json);;All Files (*)"));
    if (fname.isEmpty()) return;

    open_file(fname);
}

void MAIN_WINDOW::open_file(const QString &fname)
{
    std::ifstream is(QFile::encodeName(fname).data());
    if (!is)
    {
        QMessageBox::information(this, tr("Unable to open file"), m_open_file);
        return;
    }

    m_renderview->open(is, m_defaults);
    m_open_file = fname;

    setWindowTitle("Shoreline Renderer - " + m_open_file);

    update_parameters();
}

void MAIN_WINDOW::save_image_as()
{
    auto fname = QFileDialog::getSaveFileName(
        this,
        tr("Save Image"), "",
        tr(".png Image (*.png);;All Files (*)"));
    if (fname.isEmpty()) return;

    QImage image = m_renderview->get_qimage();
    image.save(fname);
}

void MAIN_WINDOW::update_parameters()
{
    const auto &scene = m_renderview->get_scene();
    for (auto it = scene.cbegin(); it != scene.cend(); ++it)
    {
        std::string name = it.key();
        auto p_it = m_parameters.find(name);
        if (p_it != m_parameters.end())
        {
            p_it->second->setValue(it.value());
        }
    }
}
