#-------------------------------------------------
#
# Project created by QtCreator 2019-04-26T18:18:55
#
#-------------------------------------------------

QT       += core gui network multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = dudestar_rx
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

SOURCES += \
        SHA256.cpp \
        cbptc19696.cpp \
        cgolay2087.cpp \
        chamming.cpp \
        crc.cpp \
        crs129.cpp \
        dudestar_rx.cpp \
        fec.cpp \
        main.cpp \
        mbe.cpp \
        mbefec.cpp \
        pn.cpp \
        viterbi.cpp \
        viterbi5.cpp \
        ysf.cpp

HEADERS += \
        SHA256.h \
        cbptc19696.h \
        cgolay2087.h \
        chamming.h \
        crc.h \
        crs129.h \
        dudestar_rx.h \
        fec.h \
        mbe.h \
        mbefec.h \
        mbelib_parms.h \
        pn.h \
        viterbi.h \
        viterbi5.h \
        ysf.h

FORMS += \
    dudestar_rx.ui

win32:QMAKE_LFLAGS += -static

QMAKE_LFLAGS_WINDOWS += --enable-stdcall-fixup

LIBS += -lmbe

RC_ICONS = images/dstar.ico

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    dudestar_rx.qrc
