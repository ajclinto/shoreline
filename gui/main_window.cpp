/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "main_window.h"
#include "render_view.h"
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
    m_params = new QWidget;
    m_params->setLayout(layout);
    int row = 0;
    for (const auto &json_p : m_json_ui)
    {
        const auto &name = json_p["name"];
        layout->addWidget(new QLabel(QString(name.get<std::string>().c_str()), m_params), row, 0, Qt::AlignRight);

        if (json_p["type"] == "float")
        {
            double def = json_p["default"];
            m_defaults[static_cast<std::string>(name)] = def;

            auto *sb = new QDoubleSpinBox(m_params);
            layout->addWidget(sb, row, 1);
            sb->setKeyboardTracking(false);
            sb->setMinimum(json_p["min"]);
            sb->setMaximum(json_p["max"]);
            sb->setDecimals(3);

            sb->setValue(def);

            connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [&](double value) { m_renderview->set_parameter(name, value); }
                    );

            int imax = 1000;
            auto *sl = new QSlider(Qt::Horizontal, m_params);
            layout->addWidget(sl, row, 2);
            sl->setMinimum(0);
            sl->setMaximum(imax);
            sl->setValue(static_cast<int>(imax * (def - sb->minimum()) / (sb->maximum() - sb->minimum())));

            connect(sl, &QSlider::valueChanged, this,
                    [sb, imax](int value) { sb->setValue(sb->minimum() + (value / (double)imax)*(sb->maximum() - sb->minimum())); });
        }
        else if (json_p["type"] == "int")
        {
            int def = json_p["default"];
            m_defaults[static_cast<std::string>(name)] = def;

            auto *sb = new QSpinBox(m_params);
            layout->addWidget(sb, row, 1);
            sb->setKeyboardTracking(false);
            sb->setMinimum(json_p["min"]);
            sb->setMaximum(json_p["max"]);
            sb->setValue(def);

            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [&](int value) { m_renderview->set_parameter(name, value); }
                    );

            auto *sl = new QSlider(Qt::Horizontal, m_params);
            layout->addWidget(sl, row, 2);

            auto scale_it = json_p.find("scale");
            if (scale_it != json_p.end() && *scale_it == "log")
            {
                int imin = json_p["min"];
                int imax = json_p["max"];

                auto from_log = [](int value, int imin, int imax)
                {
                    return std::min(imin*(1<<value), imax);
                };
                auto to_log = [](int value, int imin)
                {
                    int bits = 0;
                    while (value > imin)
                    {
                        bits++;
                        value >>= 1;
                    }
                    return bits;
                };

                sl->setMinimum(0);
                sl->setMaximum(to_log(imax, imin));
                sl->setValue(to_log(def, imin));

                connect(sl, &QSlider::valueChanged, this,
                        [sb,from_log,imin,imax](int value) { sb->setValue(from_log(value, imin, imax)); }
                        );
                connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), sl,
                        [sl,to_log,imin,imax](int value) { sl->blockSignals(true); sl->setValue(to_log(value, imin)); sl->blockSignals(false); }
                       );
            }
            else
            {
                sl->setMinimum(json_p["min"]);
                sl->setMaximum(json_p["max"]);
                sl->setValue(def);

                connect(sl, &QSlider::valueChanged, sb, &QSpinBox::setValue);
                connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), sl,
                        [sl](int value) { sl->blockSignals(true); sl->setValue(value); sl->blockSignals(false); }
                       );
            }
        }
        else if (json_p["type"] == "color")
        {
            const auto &def = json_p["default"];
            m_defaults[static_cast<std::string>(name)] = def;

            QColor def_color;
            def_color.setRgbF(def[0], def[1], def[2]);
            auto *cp = new COLOR_WIDGET(def_color, m_params);
            layout->addWidget(cp, row, 1);

            connect(cp, &COLOR_WIDGET::valueChanged, this,
                    [&](const QColor &value) { m_renderview->set_parameter(name, {value.redF(), value.greenF(), value.blueF()}); }
                    );
        }
        else if (json_p["type"] == "bool")
        {
            bool def = json_p["default"];
            m_defaults[static_cast<std::string>(name)] = def;

            auto *cb = new QCheckBox("", m_params);
            cb->setCheckState(def ? Qt::Checked : Qt::Unchecked);
            layout->addWidget(cb, row, 1);

            connect(cb, &QCheckBox::stateChanged, this,
                    [&](int value) { m_renderview->set_parameter(name, value != 0); }
                    );
        }
        else
        {
            assert("unknown type");
        }

        row++;
    }

    // Compacts rows
    layout->setRowStretch(row, 1);

    auto scroll = new QScrollArea;
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_params);
    m_dock->setWidget(scroll);

    m_new = new QAction(tr("&New"), this);
    m_new->setShortcut(QKeySequence::New);
    m_open = new QAction(tr("&Open"), this);
    m_open->setShortcut(QKeySequence::Open);
    m_save = new QAction(tr("&Save"), this);
    m_save->setShortcut(QKeySequence::Save);
    m_save_as = new QAction(tr("&Save As..."), this);
    m_save_as->setShortcut(QKeySequence::SaveAs);
    m_quit = new QAction(tr("&Quit"), this);
    m_quit->setShortcut(QKeySequence::Quit);

    connect(m_new, &QAction::triggered, this, &MAIN_WINDOW::reset);
    connect(m_open, &QAction::triggered, this, &MAIN_WINDOW::open);
    connect(m_save, &QAction::triggered, this, &MAIN_WINDOW::save);
    connect(m_save_as, &QAction::triggered, this, &MAIN_WINDOW::save_as);
    connect(m_quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_file_menu = menuBar()->addMenu(tr("&File"));
    m_file_menu->addAction(m_new);
    m_file_menu->addAction(m_open);
    m_file_menu->addAction(m_save);
    m_file_menu->addAction(m_save_as);
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

    m_renderview->set_scene(m_defaults);
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

    m_renderview->open(is);
    m_open_file = fname;
}
