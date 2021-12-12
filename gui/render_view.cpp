/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "render_view.h"
#include <fstream>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sstream>


RENDER_VIEW::RENDER_VIEW(QGLFormat fmt,
                         const char *progname,
                         QWidget *parent,
                         QStatusBar *status)
    : QGLWidget(fmt, parent)
    , m_statusbar(status)
    , m_program(0)
    , m_texture(0)
    , m_pbuffer(0)
{
    // Extract the path to the executable
    m_path = progname;
    size_t pos = m_path.rfind('/');
    if (pos != std::string::npos)
        m_path.resize(pos+1);
    else
        m_path = "";

    m_shm_name = "/slrender";
    m_shm_name += std::to_string(getpid());

    // For pan/zoom
    setMouseTracking(true);

    // For key events
    setFocusPolicy(Qt::ClickFocus);
    setFocus();

    makeCurrent();

    QGLFormat::OpenGLVersionFlags flags = QGLFormat::openGLVersionFlags();
    if (!(flags & QGLFormat::OpenGL_Version_3_3))
    {
        // Trying 3.0
        fmt.setVersion(3,0);
        setFormat(fmt);
    }

    // Timer to handle image updates on the pipe
    startTimer(100);
}

RENDER_VIEW::~RENDER_VIEW()
{
    stop_render();
}

bool RENDER_VIEW::start_render()
{
    stop_render();

    // For sending the scene .json
    int jsonfd[2];

    // Tile messaging
    int infd[2];
    int outfd[2];

    if (pipe(jsonfd) < 0)
    {
        perror("pipe failed");
        return false;
    }
    if (pipe(infd) < 0)
    {
        perror("pipe failed");
        return false;
    }
    if (pipe(outfd) < 0)
    {
        perror("pipe failed");
        return false;
    }

    m_child = fork();
    if (m_child == -1)
    {
        perror("fork failed");
        return false;
    }

    if (m_child == 0)
    {
        // Copy to stdin
        dup2(jsonfd[0], 0);

        // Close input for child
        ::close(jsonfd[1]);
        ::close(infd[0]);
        ::close(outfd[1]);

        const char  *slrender = "../renderer/slrender";
        const char  *args[256];
        int          sl_args = 0;

        args[sl_args++] = slrender;
        args[sl_args] = NULL;

        if (execvp(slrender, (char * const *)args) == -1)
        {
            char    buf[256];
            sprintf(buf, "Could not execute %s", slrender);
            perror(buf);
            return false;
        }
        // Unreachable
    }

    ::close(jsonfd[0]); m_outjson_fd = jsonfd[1];
    ::close(infd[1]);    m_intile_fd = infd[0];
    ::close(outfd[0]);   m_outtile_fd = outfd[1];

    // Add the tile interface details to the scene
    auto scene = m_scene;
    scene["outpipe"] = infd[1];
    scene["inpipe"] = outfd[0];
    scene["shared_mem"] = m_shm_name;

    // Write the scene to the child stdin
    std::ostringstream oss;
    oss << scene;
    std::string str = oss.str();
    if (write(m_outjson_fd, str.c_str(), str.size()) < 0)
    {
        return false;
    }

    // Read the resolution
    if (read(m_intile_fd, &m_res, sizeof(RES)) <= 0)
    {
        return false;
    }

    // Initialize shared memory after reading m_res, since at this point we
    // know the child process has created the shared buffer
    if (!init_shm())
        return false;

    // Create the tile queue for the first sample
    TILE tile;
    for (tile.yoff = 0; tile.yoff < m_res.yres; tile.yoff += m_res.tres)
    {
        for (tile.xoff = 0; tile.xoff < m_res.xres; tile.xoff += m_res.tres)
        {
            tile.xsize = std::min(m_res.xres - tile.xoff, m_res.tres);
            tile.ysize = std::min(m_res.yres - tile.yoff, m_res.tres);
            m_tiles.push(tile);
        }
    }

    m_image.resize(m_res.xres, m_res.yres);
    m_image_dirty = true;

    // Set the read fd to nonblocking for the notifier callback
    fcntl(m_intile_fd, F_SETFL, O_NONBLOCK);

    // Register for events on the socket
    m_intile_notifier = new QSocketNotifier(m_intile_fd, QSocketNotifier::Read);
    connect(m_intile_notifier, &QSocketNotifier::activated, this, &RENDER_VIEW::intile_event);

    // Queue up the initial tiles
    m_tiles_complete = 0;
    for (int i = 0; i < m_res.nthreads; i++)
    {
        send_tile(i);
    }

    return true;
}

void RENDER_VIEW::stop_render()
{
    if (m_child > 0)
    {
        kill(m_child, SIGKILL);
        m_child = 0;

        munmap(m_shm_data, m_res.shm_size());
        m_shm_data = nullptr;
        ::close(m_shm_fd); m_shm_fd = -1;

        delete m_intile_notifier;
        m_intile_notifier = nullptr;

        ::close(m_outjson_fd); m_outjson_fd = -1;
        ::close(m_intile_fd);  m_intile_fd = -1;
        ::close(m_outtile_fd); m_outtile_fd = -1;

        m_tiles = std::queue<TILE>();
    }
}

void RENDER_VIEW::store_snapshot()
{
    m_snapshot = m_image;
    m_snapshot_dirty = true;
}

void RENDER_VIEW::toggle_snapshot()
{
    if (!m_snapshot.empty())
    {
        m_snapshot_active = !m_snapshot_active;
        if (m_snapshot_active)
        {
            m_snapshot_dirty = true;
        }
        else
        {
            m_image_dirty = true;
        }
        update();
    }
}

bool
RENDER_VIEW::init_shm()
{
    // Set up shared memory before fork
    m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
    if (m_shm_fd == -1)
    {
        perror("shm_open");
        return false;
    }

    m_shm_data = (uint*)mmap(NULL, m_res.shm_size(),
            PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (m_shm_data == MAP_FAILED)
    {
        perror("mmap");
        return false;
    }

    return true;
}

// Load a file into a buffer.  The buffer is owned by the caller, and
// should be freed with delete[].
static char *
loadTextFile(const char *filename, const std::vector<std::string> &paths)
{
    std::ifstream   is;
    for (auto it = paths.begin(); it != paths.end(); ++it)
    {
        std::string path = *it + filename;

        is.open(path.c_str());
        if (is.good())
            break;
    }
    if (!is.good())
        return 0;

    is.seekg(0, std::ios::end);
    long length = is.tellg();
    is.seekg(0, std::ios::beg);

    if (!length)
        return 0;

    char *buffer = new char[length+1];

    is.read(buffer, length);
    buffer[length] = '\0';

    return buffer;
}

static bool
loadShaderProgram(QGLShader *shader,
        const char *filename,
        const char *version,
        const std::vector<std::string> &paths,
        const unsigned char *def, int deflen)
{
    char *src = loadTextFile(filename, paths);
    if (!src)
    {
        src = new char[deflen+1];
        memcpy(src, def, deflen);
        src[deflen] = '\0';
    }

    // Insert the version string into the shader source
    std::string src_with_version = version;
    src_with_version += src;
    delete [] src;

    return shader->compileSourceCode(src_with_version.c_str());
}

void
RENDER_VIEW::initializeGL()
{
    // We're doing our own dithering.  Though having this enabled didn't
    // seem to produce any dithering.
    glDisable(GL_DITHER);

    // Create the memory state texture
    glGenBuffers(1, &m_pbuffer);

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE, m_texture);

    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    std::vector<std::string> paths;
    paths.push_back("");
    paths.push_back(m_path);
    paths.push_back("/usr/share/memview/");

    QGLShader *vshader = new QGLShader(QGLShader::Vertex, this);
    QGLShader *fshader = new QGLShader(QGLShader::Fragment, this);

    const char *version;
    const QGLFormat &fmt = format();
    std::pair<int,int> version_pair(fmt.majorVersion(), fmt.minorVersion());
    if (version_pair >= std::pair<int,int>(3,3))
    {
        // OpenGL 3.3 or newer, use it
        version = "#version 330\n\n";
    }
    else
    {
        // Try compiling shader for OpenGL 3.0 (GLSL 1.30) with
        // GL_EXT_gpu_shader4 for integer textures
        version = "#version 130\n"
                  "#extension GL_EXT_gpu_shader4 : enable\n\n";
    }

    bool success = true;
    success &= loadShaderProgram(vshader, "image.vert", version, paths, 0, 0);
    success &= loadShaderProgram(fshader, "image.frag", version, paths, 0, 0);

    if (success)
    {
        m_program = new QGLShaderProgram(this);
        m_program->addShader(vshader);
        m_program->addShader(fshader);

        m_program->bindAttributeLocation("pos", 0);

        if (!m_program->link())
        {
            delete m_program;
            m_program = 0;
        }
    }
    else
    {
        delete vshader;
        delete fshader;
    }
}

void
RENDER_VIEW::resizeGL(int width, int height)
{
    glViewport(0, 0, (GLint)width, (GLint)height);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
}

void
RENDER_VIEW::paintGL()
{
    const RASTER<uint32_t> *image = nullptr;
    if (m_snapshot_active)
    {
        if (m_snapshot_dirty)
        {
            image = &m_snapshot;
            m_snapshot_dirty = false;
        }
    }
    else
    {
        if (m_image_dirty)
        {
            image = &m_image;
            m_image_dirty = false;
        }
    }

    if (image)
    {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbuffer);

        glBufferData(GL_PIXEL_UNPACK_BUFFER, image->bytes(), 0, GL_STREAM_DRAW);

        uint32_t *data = (uint32_t *)glMapBufferARB(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        assert(data);

        memcpy(data, image->data(), image->bytes());

        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_RECTANGLE, m_texture);

        glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA,
                image->width(), image->height(), 0, GL_RGBA,
                GL_UNSIGNED_BYTE, 0 /* offset in PBO */);

        // Unbind the buffer - this is required for text rendering to work
        // correctly.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    if (m_program)
    {
        m_program->bind();

        m_program->setUniformValue("s_texture", 0);
        m_program->setUniformValue("wsize", QSize(width()/2, height()/2));
        m_program->setUniformValue("off", m_offset);
        m_program->setUniformValue("zoom", m_zoom);
    }
    else
    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // Create vertex buffers for a full-screen quad (as a triangle strip of 2
    // triangles)
    const GLfloat pos[4][2] = {
        {-1, -1},
        { 1, -1},
        {-1, 1 },
        { 1, 1 }
    };
 
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint vbo;
    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(GLfloat), pos, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Delete the vertex array for each render call. This seems to be required
    // to mix vertex arrays with calls to renderText() (in paintText() below).
    glDisableVertexAttribArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);

    if (m_program)
    {
        m_program->release();
    }

    QFont font;
    QFontMetrics metrics(font);
    int tx = 4;
    int ty = metrics.height();
    QString str;
    if (m_snapshot_active)
    {
        str = "Snapshot";
    }
    else
    {
        str = "Active Render ";
        str += std::to_string(m_tiles_complete * 100 / (m_res.tile_count() * m_res.nsamples)).c_str();
        str += "%";
    }
    renderText(tx, ty, str, font);
}

bool
RENDER_VIEW::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip)
    {
        return true;
    }
    return QWidget::event(event);
}

void
RENDER_VIEW::resizeEvent(QResizeEvent *event)
{
    QGLWidget::resizeEvent(event);
    update();
}

void
RENDER_VIEW::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        // TODO: Reorder tiles
    }
}

void
RENDER_VIEW::mouseMoveEvent(QMouseEvent *event)
{
    QPoint currpos = event->pos();
    if (event->buttons() & Qt::LeftButton)
    {
        m_offset += m_mousepos - currpos;

        update();
    }
    m_mousepos = currpos;
}

void
RENDER_VIEW::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
    }
}

void
RENDER_VIEW::wheelEvent(QWheelEvent *event)
{
    const float zoomstep = 2.0F;

    float zoomratio = 1.0F;
    if (event->delta() > 0)
    {
        zoomratio = zoomstep;
    }
    else if (event->delta() < 0)
    {
        zoomratio = 1.0F / zoomstep;
    }
    else
    {
        return;
    }

    m_zoom *= zoomratio;

    // Whatever pixel the cursor is under needs to stay in place
    QPoint halfres(width()/2, height()/2);
    m_offset = (m_mousepos - halfres + m_offset) * zoomratio - m_mousepos + halfres;

    update();
}

void RENDER_VIEW::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F)
    {
        // TODO: resize main window
    }
    else if (event->key() == Qt::Key_G)
    {
        m_offset = QPoint(0, 0);
        m_zoom = 1.0;

        update();
    }
    else if (event->key() == Qt::Key_Left ||
             event->key() == Qt::Key_Right)
    {
        toggle_snapshot();
    }
    else if (event->key() == Qt::Key_Escape)
    {
        stop_render();
    }
}

void RENDER_VIEW::intile_event(int fd)
{
    assert(fd == m_intile_fd);
    TILE tile;
    ssize_t bytes = read(fd, &tile, sizeof(TILE));
    while (bytes > 0)
    {
        // Copy scanlines into the image
        for (int y = 0; y < tile.ysize; y++)
        {
            memcpy(m_image.get_scan(tile.yoff + y) + tile.xoff,
                   m_shm_data + m_res.tres*m_res.tres*tile.tid + y*tile.xsize,
                   tile.xsize*sizeof(uint));
        }

        m_tiles_complete++;
        m_image_dirty = true;

        send_tile(tile.tid);
        bytes = read(fd, &tile, sizeof(TILE));
    }
    if (bytes == 0)
    {
        // End of file (the child process exited)
        stop_render();
    }
    else if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
        // Errors other than an empty pipe
        perror("read");
    }
}

void RENDER_VIEW::send_tile(int tid)
{
    if (!m_tiles.empty())
    {
        TILE tile = m_tiles.front();
        tile.tid = tid;
        m_tiles.pop();
        if (write(m_outtile_fd, &tile, sizeof(TILE)) < 0)
        {
            perror("write failed");
        }

        // Push the next sample
        tile.sidx++;
        if (tile.sidx < m_res.nsamples)
        {
            m_tiles.push(tile);
        }
    }
}

void RENDER_VIEW::timerEvent(QTimerEvent *)
{
    if (m_image_dirty)
    {
        update();
    }
}

void RENDER_VIEW::set_parameter(const std::string &name, const nlohmann::json &value)
{
    m_scene[name] = value;
    if (!m_image.empty()) start_render();
}

void RENDER_VIEW::open(std::istream &is, const nlohmann::json &defs)
{
    is >> m_scene;

    for (const auto &p : defs.items())
    {
        auto it = m_scene.find(p.key());
        if (it == m_scene.end())
        {
            m_scene[p.key()] = p.value();
        }
    }

    start_render();
}

QImage RENDER_VIEW::get_qimage() const
{
    const RASTER<uint32_t> *image = nullptr;
    if (m_snapshot_active)
    {
        image = &m_snapshot;
    }
    else
    {
        image = &m_image;
    }

    return QImage(reinterpret_cast<const unsigned char *>(image->data()), image->width(), image->height(), QImage::Format_RGBA8888);
}

