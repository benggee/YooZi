QT += core websockets widgets

CONFIG += c++17

TARGET = windows-proxy
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/ws_client.cpp \
    src/command_executor.cpp \
    src/main_window.cpp \
    src/settings_dialog.cpp \
    src/tray_icon.cpp

HEADERS += \
    src/ws_client.h \
    src/command_executor.h \
    src/main_window.h \
    src/settings_dialog.h \
    src/tray_icon.h \
    src/protocol.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
