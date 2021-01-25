QT -= gui

CONFIG += c++2a

QMAKE_LINK = g++-10
QMAKE_CXX = g++-10
QMAKE_CC = gcc-10

QMAKE_CXXFLAGS += -fno-omit-frame-pointer

CONFIG += object_parallel_to_source

#QMAKE_CXXFLAGS += -O3 -march=native -flto -fuse-ld=gold -fuse-linker-plugin
QMAKE_CXXFLAGS += -Wunused -Wunused-function -msse4.2
QMAKE_CXXFLAGS += -O0 -ggdb3
#to suppress an error in QVariant
QMAKE_CXXFLAGS += -Wno-deprecated-copy
QMAKE_CXXFLAGS += -Wall #-flto -fuse-ld=gold -fuse-linker-plugin -Os
#QMAKE_LFLAGS += -flto
# these 3 lines can be used to analyze memory related problems
#QMAKE_CXXFLAGS+= -fsanitize=address -fno-omit-frame-pointer
#QMAKE_CFLAGS+= -fsanitize=address -fno-omit-frame-pointer
#QMAKE_LFLAGS+= -fsanitize=address -fno-omit-frame-pointer




CONFIG += console
CONFIG -= app_bundle

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        main.cpp



include(minMysql/minMysql.pri)
include(nanoSpammer/nanoSpammer.pri)
include(minCurl/minCurl.pri)
include(QStacker/QStacker.pri)
include(fileFunction/fileFunction.pri)
include(fmt/fmt.pri)
