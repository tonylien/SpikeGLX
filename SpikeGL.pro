######################################################################
# Automatically generated by qmake (2.01a) Tue Apr 21 18:41:54 2009
# Manually edited since then! :)
######################################################################

# Select real or simulated {IM,NI}:
DEFINES += HAVE_IMEC
DEFINES += HAVE_NIDAQmx

TEMPLATE = app

contains(DEFINES, HAVE_NIDAQmx) {
    TARGET = SpikeGLX
}
else {
    TARGET = SpikeGLX_NISIM
}

contains(CONFIG, debug) {
    DESTDIR = C:/Users/karshb/Desktop/DEBUG53
#DESTDIR = Y:/__billKarsh__/SPIKEGL/DEBUG53
#DESTDIR = Y:/__billKarsh__/SPIKEGL/DEBUG54
#DESTDIR = Y:/__billKarsh__/SPIKEGL/DEBUG55
#DESTDIR = Y:/__billKarsh__/SPIKEGL/DEBUG56mingw
#DESTDIR = Y:/__billKarsh__/SPIKEGL/DEBUG56MSVC
#DESTDIR = Y:/__billKarsh__/SPIKEGL/DEBUG56-64
}
else {
    DESTDIR = C:/Users/karshb/Desktop/SpikeGLX53
#DESTDIR = Y:/__billKarsh__/SPIKEGL/SpikeGLX53
#DESTDIR = Y:/__billKarsh__/SPIKEGL/SpikeGLX54
#DESTDIR = Y:/__billKarsh__/SPIKEGL/SpikeGLX55
#DESTDIR = Y:/__billKarsh__/SPIKEGL/SpikeGLX56mingw
#DESTDIR = C:/Users/karshb/Desktop/SpikeGLX56MSVC
#DESTDIR = Y:/__billKarsh__/SPIKEGL/SpikeGLX56MSVC
#DESTDIR = Y:/__billKarsh__/SPIKEGL/SpikeGLX56-64
}

DEPENDPATH  += $$PWD
INCLUDEPATH += $$PWD

QT += opengl network svg

# Our sources
SRC_SGLX = \
    Src-audio \
    Src-datafile \
    Src-filters \
    Src-gates \
    Src-graphs \
    Src-gui_tools \
    Src-main \
    Src-params \
    Src-remote \
    Src-run \
    Src-triggers \
    Src-verify
for(dir, SRC_SGLX) {
    INCLUDEPATH += $$PWD/$$dir
    include($$dir/$$dir".pri")
}

# 3rd party
SRC_ALIEN = \
    RtAudio \
    Samplerate
for(dir, SRC_ALIEN) {
    INCLUDEPATH += $$PWD/$$dir
    include($$dir/$$dir".pri")
}

# Resources
RSRC = \
    Forms \
    Resources
for(dir, RSRC) {
    include($$dir/$$dir".pri")
}

# Docs
OTHER_FILES += \
    Agenda.txt \
    LICENSE.txt \
    README.md

win32 {
# Note: RtAudio support:
#   "LIBS += -lole32 -lwinmm -lksuser -luuid -ldsound"
#   "DEFINES += __WINDOWS_ASIO__"
#   "DEFINES += __WINDOWS_WASAPI__"
#   "DEFINES += __WINDOWS_DS__"
# Note: CniAcqDmx GetProcessMemoryInfo() support:
#   "LIBS += -lpsapi"
# Note: Switch QGLWidget to QOpenGLWidget: enable:
#   "DEFINES += OPENGL54"
# Note: This 32-bit MinGW app uses MEM > 2GB:
#   "QMAKE_LFLAGS += -Wl,--large-address-aware"

    contains(DEFINES, HAVE_IMEC) {
        QMAKE_LIBDIR    += $${_PRO_FILE_PWD_}/IMEC
#        LIBS            += -llibNeuropix_basestation_api_msvc
        LIBS            += -llibNeuropix_basestation_api
    }

    contains(DEFINES, HAVE_NIDAQmx) {
        QMAKE_LIBDIR    += $${_PRO_FILE_PWD_}/NI
        LIBS            += -lNIDAQmx
    }

    CONFIG          += embed_manifest_exe
    LIBS            += -lWS2_32 -lUser32
    LIBS            += -lopengl32 -lglu32
    LIBS            += -lole32 -lwinmm -lksuser -luuid -ldsound
    LIBS            += -lpsapi
#    DEFINES         += __WINDOWS_ASIO__
#    DEFINES         += __WINDOWS_WASAPI__
    DEFINES         += __WINDOWS_DS__
#    DEFINES         += OPENGL54
    DEFINES         += _CRT_SECURE_NO_WARNINGS WIN32
    QMAKE_LFLAGS    += -Wl,--large-address-aware
}

unix {
    CONFIG          += debug warn_on
#   QMAKE_CFLAGS    += -Wall -Wno-return-type
#   QMAKE_CXXFLAGS  += -Wall -Wno-return-type
# Enable these for profiling!
#   QMAKE_CFLAGS    += -pg
#   QMAKE_CXXFLAGS  += -pg
#   QMAKE_LFLAGS    += -pg
}

macx {
    LIBS    += -framework CoreServices
    DEFINES += MACX
}


