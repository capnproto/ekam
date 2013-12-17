# Kenton's Code Playground -- http://code.google.com/p/kentons-code
# Author: Kenton Varda (temporal@gmail.com)
# Copyright (c) 2010 Google, Inc. and contributors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

TARGET = EkamDashboard
TEMPLATE = lib

DEFINES += EKAMDASHBOARD_LIBRARY

# EkamDashboard files

SOURCES += ekamdashboardplugin.cpp \
    ekamtreewidget.cpp

HEADERS += ekamdashboardplugin.h\
        ekamdashboard_global.h\
        ekamdashboardconstants.h \
    ekamtreewidget.h

PROTOS += dashboard.proto

INCLUDEPATH += .

protobuf_header.name = protobuf header
protobuf_header.input = PROTOS
protobuf_header.output  = ${QMAKE_FILE_BASE}.pb.h
protobuf_header.commands = protoc --cpp_out="." -I`dirname ${QMAKE_FILE_NAME}` ${QMAKE_FILE_NAME}
protobuf_header.variable_out = GENERATED_FILES
QMAKE_EXTRA_COMPILERS += protobuf_header

protobuf_src.name  = protobuf src
protobuf_src.input = PROTOS
protobuf_src.output  = ${QMAKE_FILE_BASE}.pb.cc
protobuf_src.depends  = ${QMAKE_FILE_BASE}.pb.h
protobuf_src.commands = true
protobuf_src.variable_out = GENERATED_SOURCES
QMAKE_EXTRA_COMPILERS += protobuf_src

# Qt Creator linking

## set the QTC_SOURCE environment variable to override the setting here
QTCREATOR_SOURCES = $$(QTC_SOURCE)
isEmpty(QTCREATOR_SOURCES):QTCREATOR_SOURCES=/home/kenton/src/qt-creator-opensource-src-3.0.0

## set the QTC_BUILD environment variable to override the setting here
IDE_BUILD_TREE = $$(QTC_BUILD)
isEmpty(IDE_BUILD_TREE):IDE_BUILD_TREE=/home/kenton/src/qt-creator-opensource-src-3.0.0

## uncomment to build plugin into user config directory
## <localappdata>/plugins/<ideversion>
##    where <localappdata> is e.g.
##    "%LOCALAPPDATA%\Nokia\qtcreator" on Windows Vista and later
##    "$XDG_DATA_HOME/Nokia/qtcreator" or "~/.local/share/data/Nokia/qtcreator" on Linux
##    "~/Library/Application Support/Nokia/Qt Creator" on Mac
# USE_USER_DESTDIR = yes

PROVIDER = KentonVarda

###### If the plugin can be depended upon by other plugins, this code needs to be outsourced to
###### <dirname>_dependencies.pri, where <dirname> is the name of the directory containing the
###### plugin's sources.

QTC_PLUGIN_NAME = EkamDashboard
QTC_LIB_DEPENDS += \
    # nothing here at this time

QTC_PLUGIN_DEPENDS += \
    coreplugin \
    projectexplorer \
    cpptools \
    texteditor \
    locator \
    find

QTC_PLUGIN_RECOMMENDS += \
    # optional plugin dependencies. nothing here at this time

###### End _dependencies.pri contents ######

QT += network

include($$QTCREATOR_SOURCES/src/qtcreatorplugin.pri)

LIBS += -L$$IDE_PLUGIN_PATH/Nokia -lprotobuf

RESOURCES += \
    ekamdashboard.qrc

