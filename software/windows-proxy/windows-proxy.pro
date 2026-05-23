QT += core websockets network widgets

CONFIG += c++17

TARGET = windows-proxy
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/ws_client.cpp \
    src/command_executor.cpp \
    src/ollama_client.cpp \
    src/tray_icon.cpp

HEADERS += \
    src/ws_client.h \
    src/command_executor.h \
    src/ollama_client.h \
    src/tray_icon.h \
    src/protocol.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target