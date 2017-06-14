QT += gui network
DEFINES += QTCSSH_LIBRARY

include(../../qtcreatorlibrary.pri)

SOURCES = $$PWD/sshsendfacility.cpp \
    $$PWD/sshremoteprocess.cpp \
    $$PWD/sshpacketparser.cpp \
    $$PWD/sshpacket.cpp \
    $$PWD/sshoutgoingpacket.cpp \
    $$PWD/sshkeygenerator.cpp \
    $$PWD/sshkeyexchange.cpp \
    $$PWD/sshincomingpacket.cpp \
    $$PWD/sshcryptofacility.cpp \
    $$PWD/sshconnection.cpp \
    $$PWD/sshchannelmanager.cpp \
    $$PWD/sshchannel.cpp \
    $$PWD/sshcapabilities.cpp \
    $$PWD/sftppacket.cpp \
    $$PWD/sftpoutgoingpacket.cpp \
    $$PWD/sftpoperation.cpp \
    $$PWD/sftpincomingpacket.cpp \
    $$PWD/sftpdefs.cpp \
    $$PWD/sftpchannel.cpp \
    $$PWD/sshremoteprocessrunner.cpp \
    $$PWD/sshconnectionmanager.cpp \
    $$PWD/sshkeypasswordretriever.cpp \
    $$PWD/sftpfilesystemmodel.cpp \
    $$PWD/sshkeycreationdialog.cpp \
    $$PWD/sshdirecttcpiptunnel.cpp \
    $$PWD/sshlogging.cpp \
    $$PWD/sshhostkeydatabase.cpp \
    $$PWD/sshtcpipforwardserver.cpp \
    $$PWD/sshtcpiptunnel.cpp \
    $$PWD/sshforwardedtcpiptunnel.cpp \
    $$PWD/sshagent.cpp \
    $$PWD/sshx11channel.cpp \
    $$PWD/sshx11inforetriever.cpp

HEADERS = $$PWD/sshsendfacility_p.h \
    $$PWD/sshremoteprocess.h \
    $$PWD/sshremoteprocess_p.h \
    $$PWD/sshpacketparser_p.h \
    $$PWD/sshpacket_p.h \
    $$PWD/sshoutgoingpacket_p.h \
    $$PWD/sshkeygenerator.h \
    $$PWD/sshkeyexchange_p.h \
    $$PWD/sshincomingpacket_p.h \
    $$PWD/sshexception_p.h \
    $$PWD/ssherrors.h \
    $$PWD/sshcryptofacility_p.h \
    $$PWD/sshconnection.h \
    $$PWD/sshconnection_p.h \
    $$PWD/sshchannelmanager_p.h \
    $$PWD/sshchannel_p.h \
    $$PWD/sshcapabilities_p.h \
    $$PWD/sshbotanconversions_p.h \
    $$PWD/sftppacket_p.h \
    $$PWD/sftpoutgoingpacket_p.h \
    $$PWD/sftpoperation_p.h \
    $$PWD/sftpincomingpacket_p.h \
    $$PWD/sftpdefs.h \
    $$PWD/sftpchannel.h \
    $$PWD/sftpchannel_p.h \
    $$PWD/sshremoteprocessrunner.h \
    $$PWD/sshconnectionmanager.h \
    $$PWD/sshpseudoterminal.h \
    $$PWD/sshkeypasswordretriever_p.h \
    $$PWD/sftpfilesystemmodel.h \
    $$PWD/sshkeycreationdialog.h \
    $$PWD/ssh_global.h \
    $$PWD/sshdirecttcpiptunnel_p.h \
    $$PWD/sshdirecttcpiptunnel.h \
    $$PWD/sshlogging_p.h \
    $$PWD/sshhostkeydatabase.h \
    $$PWD/sshtcpipforwardserver.h \
    $$PWD/sshtcpipforwardserver_p.h \
    $$PWD/sshtcpiptunnel_p.h \
    $$PWD/sshforwardedtcpiptunnel.h \
    $$PWD/sshforwardedtcpiptunnel_p.h \
    $$PWD/sshagent_p.h \
    $$PWD/sshx11channel_p.h \
    $$PWD/sshx11displayinfo_p.h \
    $$PWD/sshx11inforetriever_p.h

FORMS = $$PWD/sshkeycreationdialog.ui

RESOURCES += $$PWD/ssh.qrc

include(../botan/botan.pri)
use_system_botan {
    CONFIG += link_pkgconfig
    PKGCONFIG += botan-2
} else {
    BOTAN_BUILD_DIR = $$OUT_PWD/../botan/$$BOTAN_BUILD_DIR
    INCLUDEPATH += $$BOTAN_BUILD_DIR/build/include
    LIBS += $$BOTAN_BUILD_DIR/$$BOTAN_FULL_NAME
    win32: LIBS += -ladvapi32 -luser32 -lws2_32
}
msvc:QMAKE_CXXFLAGS += /wd4250
