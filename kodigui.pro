QT       += core gui network widgets webenginewidgets webenginecore concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    playerwindow.cpp \
    mainwindow.cpp \
    m3u8interceptor.cpp 

HEADERS += \
    playerwindow.h \
    mainwindow.h \
    m3u8interceptor.h

FORMS += \
    kodigui.ui \
    playerwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Windows-specific configuration
win32 {
    # libssh2 paths for Windows (adjust according to your installation)
    LIBS += -L$$PWD/libs/libssh2/lib -lssh2
    INCLUDEPATH += $$PWD/libs/libssh2/include
    
    # OpenSSL (usually required by libssh2 on Windows)
    LIBS += -L$$PWD/libs/openssl/lib -lssl -lcrypto
    INCLUDEPATH += $$PWD/libs/openssl/include
}

# Unix-specific configuration
unix:!win32 {
    LIBS += -L/usr/local/lib -lssh2
    INCLUDEPATH += /usr/local/include
}

RESOURCES += \
   resource.qrc
