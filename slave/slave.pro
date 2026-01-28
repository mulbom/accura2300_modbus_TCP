#-------------------------------------------------
#
# Project created by QtCreator 2025-09-09T09:52:40
#
#-------------------------------------------------

QT       += core gui network

CONFIG += c++11

QMAKE_CXXFLAGS += -std=gnu++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = slave
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp

HEADERS  += mainwindow.h

FORMS    += mainwindow.ui
