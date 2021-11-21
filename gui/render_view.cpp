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

    // For pan/zoom
    setMouseTracking(true);

    // For key events
    setFocusPolicy(Qt::ClickFocus);
    setFocus();

    // Start a render (move to Render button)
    if (!start_render())
    {
        fprintf(stderr, "Failed to start render\n");
        exit(1);
    }

    makeCurrent();

    QGLFormat::OpenGLVersionFlags flags = QGLFormat::openGLVersionFlags();
    if (!(flags & QGLFormat::OpenGL_Version_3_3))
    {
        // Trying 3.0
        fmt.setVersion(3,0);
        setFormat(fmt);
    }
}

RENDER_VIEW::~RENDER_VIEW()
{
    if (m_child > 0)
        kill(m_child, SIGKILL);

    if (m_inpipe) fclose(m_inpipe);
    if (m_outpipe) fclose(m_outpipe);

    if (m_shared_data)
        shm_unlink(m_shared_name.c_str());
}

bool RENDER_VIEW::start_render()
{
    int                infd[2];
    int                outfd[2];

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

    if (!init_shared_memory())
        return false;

    m_child = fork();
    if (m_child == -1)
    {
        perror("fork failed");
        return false;
    }

    if (m_child == 0)
    {
        // Close input for child
        ::close(infd[0]);
        ::close(outfd[1]);

		const char			*slrender = "../renderer/slrender";
		const char			*args[256];
		char				 inpipearg[64];
		char				 outpipearg[64];
		int					 sl_args = 0;

        args[sl_args++] = slrender;

        args[sl_args++] = "--shared_mem";
        args[sl_args++] = m_shared_name.c_str();

        args[sl_args++] = "--outpipe";
        sprintf(inpipearg, "%d", infd[1]);
        args[sl_args++] = inpipearg;

        args[sl_args++] = "--inpipe";
        sprintf(outpipearg, "%d", outfd[0]);
        args[sl_args++] = outpipearg;

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

    // Close output for parent
    ::close(infd[1]);
    ::close(outfd[0]);

    // Open the pipe for reading
    m_inpipe_fd = infd[0];
    fcntl(m_inpipe_fd, O_NONBLOCK);
    m_inpipe = fdopen(m_inpipe_fd, "r");

    // Open the pipe for writing
    m_outpipe_fd = outfd[1];
    m_outpipe = fdopen(m_outpipe_fd, "w");

    // Read the resolution
    RES res;
    if (!read(m_inpipe_fd, &res, sizeof(RES)))
    {
        return false;
    }

    // Create the tile queue
    int tcount = res.tile_count();
    m_tiles.reserve(tcount);
    TILE tile;
    for (tile.yoff = 0; tile.yoff < res.yres; tile.yoff += res.tres)
    {
        for (tile.xoff = 0; tile.xoff < res.xres; tile.xoff += res.tres)
        {
            tile.xsize = std::min(res.xres - tile.xoff, res.tres);
            tile.ysize = std::min(res.yres - tile.yoff, res.tres);
            m_tiles.push_back(tile);
        }
    }
    m_current_tile = 0;

    m_image.resize(res.xres, res.yres);
    m_image_dirty = true;

    // Register for events on the socket
    m_inpipe_notifier = new QSocketNotifier(m_inpipe_fd, QSocketNotifier::Read);
    connect(m_inpipe_notifier, SIGNAL(activated(int)), this, SLOT(socket_event(int)));

    return true;
}

bool
RENDER_VIEW::init_shared_memory()
{
    m_shared_name = "/slrender";
    m_shared_name += std::to_string(getpid());

    // Set up shared memory before fork
    int shm_fd = shm_open(m_shared_name.c_str(),
            O_CREAT | O_CLOEXEC | O_RDWR,
            S_IRUSR | S_IWUSR);
    if (shm_fd == -1)
    {
        perror("shm_open");
        return false;
    }

    size_t shm_size = 64*64*sizeof(uint);
    if (ftruncate(shm_fd, shm_size) == -1)
    {
        perror("ftruncate");
        return false;
    }

    m_shared_data = (uint*)mmap(NULL, shm_size,
            PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (m_shared_data == MAP_FAILED)
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
    if (m_image_dirty)
    {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbuffer);

        glBufferData(GL_PIXEL_UNPACK_BUFFER, m_image.bytes(), 0, GL_STREAM_DRAW);

        uint32_t *data = (uint32_t *)glMapBufferARB(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        assert(data);

        memcpy(data, m_image.data(), m_image.bytes());

        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_RECTANGLE, m_texture);

        glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA,
                m_image.width(), m_image.height(), 0, GL_RGBA,
                GL_UNSIGNED_BYTE, 0 /* offset in PBO */);

        // Unbind the buffer - this is required for text rendering to work
        // correctly.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        m_image_dirty = false;
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
}

void RENDER_VIEW::socket_event(int fd)
{
    assert(fd == m_inpipe_fd);
    int msg;
    while (read(fd, &msg, sizeof(int)))
    {
        if (msg == ASKTILE)
        {
            assert(m_current_tile < (int)m_tiles.size());
            TILE tile = m_tiles[m_current_tile++];
            if (!write(m_outpipe_fd, &tile, sizeof(TILE)))
            {
                perror("write failed");
            }
        }
        else if (msg == TILEDATA)
        {
            TILE tile;
            if (!read(fd, &tile, sizeof(TILE)))
            {
                perror("read failed");
            }

            // Copy scanlines into the image
            for (int y = 0; y < tile.ysize; y++)
            {
                memcpy(m_image.get_scan(tile.yoff + y) + tile.xoff,
                       m_shared_data + y*tile.xsize, tile.xsize*sizeof(uint));
            }
            m_image_dirty = true;
        }
    }
    if (m_image_dirty)
    {
        update();
    }
}
