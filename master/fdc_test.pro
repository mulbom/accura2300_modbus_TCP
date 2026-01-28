#-------------------------------------------------
#
# Project created by QtCreator 2025-09-08T09:43:12
#
#-------------------------------------------------

#contains(FEDORA_VERSION, 14) {
exists(/lib/modules/*fc14*) {
        # Fedora 14
        INCLUDEPATH += /usr/include/qwt
        LIBS        += -lqwt
}
else {
        exists(/usr/lib64/libqwt.so.5) {
        # Fedora 19
            INCLUDEPATH += /usr/include/qwt5-qt4
            LIBS        += -lqwt5-qt4
        }
        else {
            INCLUDEPATH += /usr/include/qwt
            LIBS        += -lqwt

            DEFINES += _QWT6
        }
}

QT       += core gui network

CONFIG += c++11

QMAKE_CXXFLAGS += -std=gnu++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = fdc_test
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp

HEADERS  += mainwindow.h

FORMS    += mainwindow.ui
