# Plain executable used as the entry point of the .bar.
TEMPLATE = app
TARGET = tidal
CONFIG += qt warn_on release
QT += declarative network
QT -= gui
LIBS += -lbbcascades -lbbmultimedia -lbbsystem -lbb
QMAKE_LFLAGS += -Wl,-rpath-link,$${QNX_DIR}/$${QNX_CPUDIR}/usr/lib/qt4/lib
QMAKE_CXXFLAGS += -Wno-psabi

SOURCES += src/main.cpp src/app.cpp src/player.cpp src/webimageview.cpp src/downloads.cpp src/localserver.cpp
HEADERS += src/app.hpp src/player.hpp src/webimageview.hpp src/downloads.hpp src/localserver.hpp

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc
