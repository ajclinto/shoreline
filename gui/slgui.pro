######################################################################
# Automatically generated by qmake (2.01a) Sun Nov 20 14:26:50 2011
######################################################################

QT += opengl

TEMPLATE = app
DEPENDPATH += .
INCLUDEPATH += .

LIBS += -lrt

QMAKE_CFLAGS_RELEASE = -DGL_GLEXT_PROTOTYPES -g -O3
QMAKE_CXXFLAGS_RELEASE = -DGL_GLEXT_PROTOTYPES -g -O3 -std=c++0x

# Input
HEADERS += main_window.h render_view.h
SOURCES += main.cpp main_window.cpp render_view.cpp
