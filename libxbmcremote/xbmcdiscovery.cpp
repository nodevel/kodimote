/*****************************************************************************
 * Copyright: 2011-2013 Michael Zanetti <michael_zanetti@gmx.net>            *
 *                                                                           *
 * This file is part of Xbmcremote                                           *
 *                                                                           *
 * Xbmcremote is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU General Public License as published by      *
 * the Free Software Foundation, either version 3 of the License, or         *
 * (at your option) any later version.                                       *
 *                                                                           *
 * Xbmcremote is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License for more details.                              *
 *                                                                           *
 * You should have received a copy of the GNU General Public License         *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.     *
 *                                                                           *
 ****************************************************************************/

#include "xbmcdiscovery.h"
#include "xbmchostmodel.h"
#include "xbmc.h"
#include "xdebug.h"

#include <QNetworkInterface>
#include <QStringList>

#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>

// Zeroconf query for _xbmc-jsonrpc._tcp, _http._tcp and _workstation._tcp

#define ADJUST(x) datagram = datagram.right(datagram.length() - x);
#define READLEN len = data.left(4).toInt(0, 16);

char query_jsonrpc_http_workstations[] = {
    // DNS-SD Header
    0x00, 0x00, 0x00, 0x00, // Flags (Standard Query)
    0x00, 0x03,             // Questions
    0x00, 0x00,             // Answer records
    0x00, 0x00,             // Authority records
    0x00, 0x00,             // Additional records

    // Question for _xbmc-jsonrpc._tcp
    0x0d,                   // Length of following ttext ield
    0x5f, 0x78, 0x62, 0x6d, 0x63, 0x2d, 0x6a, 0x73, // _xbmc-js
    0x6f, 0x6e, 0x72, 0x70, 0x63, 0x04, 0x5f, 0x74, // onrpc._t
    0x63, 0x70, 0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, // cp.local
    0x00,                   // \0
    0x00, 0x0c,             // Type PTR
    0x00, 0x01,             // Class: IN

    // Question for _http._tcp
    0x05,                   // Length of text field
    0x5f, 0x68, 0x74, 0x74, 0x70, 0x04, 0x5f, 0x74,  // _http._t
    0x63, 0x70, 0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c,  // cp.local
    0x00,                   // \0
    0x00, 0x0c,             // Type PTR
    0x00 ,0x01,             // Class IN

    // Question for _workstation._tcp
    0x0c,                   // Length of text field
    0x5f, 0x77, 0x6f, 0x72, 0x6b, 0x73, 0x74, 0x61,  // _worksta
    0x74, 0x69, 0x6f, 0x6e, 0x04, 0x5f, 0x74, 0x63,  // tion._tc
    0x70, 0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c,        // p.local
    0x00,                   // \0
    0x00, 0x0c,             // Type PTR
    0x00, 0x01              // Class IN
};

XbmcDiscovery::XbmcDiscovery(QObject *parent) :
    QObject(parent)
{

    m_socket = new QUdpSocket(this);
    m_socket->bind(5353, QUdpSocket::ShareAddress);

    m_multicastAddress = QHostAddress("224.0.0.251");
    setMulticastGroup(m_multicastAddress, true);
    connect(m_socket, SIGNAL(readyRead()), SLOT(readDatagram()));

    discover();

    connect(&m_continuousDiscoveryTimer, SIGNAL(timeout()), SLOT(discover()));
}

XbmcDiscovery::~XbmcDiscovery()
{
    setMulticastGroup(m_multicastAddress, false);
    delete m_socket;
}

void XbmcDiscovery::discover()
{
    xDebug(XDAREA_DISCOVERY) << "Sending Zeroconf Query packet";
    qDebug() << "Sending Zeroconf Query packet";
    m_socket->writeDatagram(QByteArray(query_jsonrpc_http_workstations, 93), m_multicastAddress, 5353);
}

bool XbmcDiscovery::continuousDiscovery()
{
    return m_continuousDiscoveryTimer.isActive();
}

void XbmcDiscovery::readDatagram()
{
    xDebug(XDAREA_DISCOVERY) << "***** multicast packet recieve *****";


    while (m_socket->hasPendingDatagrams()) {
        int size = m_socket->pendingDatagramSize();
        qDebug() << "**** next datagram **** size:" << size;
        char *data = (char*)malloc(size);
        m_socket->readDatagram(data, size);
        QByteArray datagram = QByteArray(data, size).toHex();
        QByteArray fullDatagram = datagram;
        free(data);
        qDebug() << datagram;

        // ignore transaction id
        ADJUST(4);

        // ignore flags
        ADJUST(4);

        // read number of queries in this datagram
        int queryCount = datagram.left(4).toInt(0, 16);
        ADJUST(4)
        if(queryCount > 0) {
            qDebug() << "there are" << queryCount << "queries in the datagram";
        }

        // read number of answer records in this datagram
        int answerCount = datagram.left(4).toInt(0, 16);
        ADJUST(4)
        if(answerCount > 0) {
            xDebug(XDAREA_DISCOVERY) << "there are" << answerCount << "answer records in the datagram";
        } else {
            xDebug(XDAREA_DISCOVERY) << "No answers... ignoring";
            continue;
        }

        // read number of authority records in this datagram
        int authorityCount = datagram.left(4).toInt(0, 16);
        ADJUST(4)
        if(authorityCount > 0) {
            xDebug(XDAREA_DISCOVERY) << "there are" << authorityCount << "authority records in the datagram";
        }

        // ignore rest
        datagram = datagram.right(datagram.length() - 4);
        qDebug() << datagram;

        XbmcHost host;

        for (int i = 0; i < queryCount; ++i) {
            int queryLen = datagram.left(2).toInt(0, 16);
            qDebug() << "discarding query" << queryLen << datagram;
            datagram = datagram.right(datagram.length() - 2);

            datagram = datagram.right(datagram.length() - (queryLen * 2));

            if (datagram.startsWith("c01a")) {
                datagram = datagram.right(datagram.length() - 4);
            } else {
                datagram = datagram.right(datagram.length() - (queryLen * 2) + 2);
            }

            datagram = datagram.right(datagram.length() - 8);
            qDebug() << "discarded query" << datagram;
        }

        int len = 0;
        while(!datagram.isEmpty()) {
            qDebug() << "starting record:" << datagram;
            QString currentService;

            if(!(datagram.startsWith("c"))) {
                //qDebug() << datagram;

                len = datagram.left(2).toInt(0, 16);
                //qDebug() << "got a pointer record with len" << len;
                datagram = datagram.right(datagram.length() - 2);
                currentService = QString(QByteArray::fromHex(datagram.left(len * 2)));;
                datagram = datagram.right(datagram.length() - len * 2);
                qDebug() << "*SERVICE:" << currentService;

                if(datagram.startsWith("c0")) {
                    // its a reference name.. ignore it
                    qDebug() << "ignoring reference";
                    datagram = datagram.right(datagram.length() - 4);
                } else {
                    len = datagram.left(2).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 2);
                    qDebug() << "proto:" << QString(QByteArray::fromHex(datagram.left(len * 2)));
                    datagram = datagram.right(datagram.length() - len * 2);

                    len = datagram.left(2).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 2);
                    qDebug() << "domain:" << QString(QByteArray::fromHex(datagram.left(len * 2)));
                    datagram = datagram.right(datagram.length() - len * 2);

                    // trim 0 termination
                    datagram = datagram.right(datagram.length() - 2);
                }
                // ignore flags:
                datagram = datagram.right(datagram.length() - 16);

                qDebug() << "datagram" << datagram;

                len = datagram.left(4).toInt(0, 16);
                datagram = datagram.right(datagram.length() - 4);
                QString domain = QString(QByteArray::fromHex(datagram.left(len * 2)));
                datagram = datagram.right(datagram.length() - len * 2);
                xDebug(XDAREA_DISCOVERY) << "fqdn:" << domain << domain.length() << len;

                if(currentService == "_workstation") {
                    domain = domain.right(domain.length() - 1);
                    QString hostname = domain.split("[").first();
                    hostname = hostname.left(hostname.length() - 1);
                    qDebug() << "hostname:" << hostname;
                    host.setHostname(hostname);

                    if(domain.split('[').count() > 1) {
                        QString hwAddr = domain.split("[").at(1);
                        hwAddr = hwAddr.left(17);
                        host.setHwAddr(hwAddr);
                    }
                } else if(currentService == "_http") {
                    qDebug() << "is xbmcHttp record!";
                    host.setXbmcHttpSupported(true);
                } else if(currentService == "_xbmc-jsonrpc") {
                    host.setXbmcJsonrpcSupported(true);
                }


            } else {
                qDebug() << "got a 2er record" << datagram[0]<<datagram[1];
                datagram = datagram.right(datagram.length() - 1);

                // ignore name
                int index = datagram.left(3).toInt(0, 16);
                if (datagram[1] == '!') {

                }
                QString serviceName = extractName(fullDatagram, index);
                qDebug() << "got index" << index << serviceName;

                datagram = datagram.right(datagram.length() - 3);

                // type
                if(datagram.startsWith("0010")) {
                    qDebug() << "got TXT record";
                    datagram = datagram.right(datagram.length() - 4);

                    // ignoring flags
                    datagram = datagram.right(datagram.length() - 12);

                    len = datagram.left(4).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 4);
                    qDebug() << "Text:" << QString(QByteArray::fromHex(datagram.left(len * 2))) << "len:" << len;
                    datagram = datagram.right(datagram.length() - len * 2);

                } else if(datagram.startsWith("0021")) {
                    qDebug() << "got SRV record";
                    datagram = datagram.right(datagram.length() - 4);

                    // ignoring flags
                    datagram = datagram.right(datagram.length() - 12);

                    // get length
                    len = datagram.left(4).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 4);
                    qDebug() << "record length:" << len;

                    // ignoring priority
                    if(len > 0) {
                        len -= 2;
                        datagram = datagram.right(datagram.length() - 4);
                    }

                    // ignoring weight
                    if(len > 0) {
                        len -= 2;
                        datagram = datagram.right(datagram.length() - 4);
                    }

                    // Get Port
                    if(len > 0) {
                        len -= 2;
                        int port = datagram.left(4).toInt(0, 16);
                        qDebug() << "Port:" << port;
                        datagram = datagram.right(datagram.length() - 4);

                        qDebug() << "it is still?" << serviceName;


                        if(serviceName.contains("_http")) {
                            host.setPort(port);
                            host.setXbmcHttpSupported(true);
                        }
                        if (serviceName.contains("_xbmc-jsonrpc")) {
                            host.setXbmcJsonrpcSupported(true);
                        }
                    }

                    // ignore rest:
                    datagram = datagram.right(datagram.length() - len * 2);

                } else if(datagram.startsWith("001c")) {
                    qDebug() << "got a type AAA record";
                    datagram = datagram.right(datagram.length() - 4);

                    // ignoring flags
                    datagram = datagram.right(datagram.length() - 12);

                    // ipv6 addr
                    len = datagram.left(4).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 4);
                    qDebug() << "IPv6 Address:" << QString(QByteArray::fromHex(datagram.left(len * 2))) << "len:" << len;
                    datagram = datagram.right(datagram.length() - len * 2);

                } else if(datagram.startsWith("0001")) {
                    qDebug() << "got a type A record";
                    datagram = datagram.right(datagram.length() - 4);

                    // ignoring flags
                    datagram = datagram.right(datagram.length() - 12);

                    // ipv4 addr
                    len = datagram.left(4).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 4);

                    int ip1 = datagram.left(2).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 2);
                    int ip2 = datagram.left(2).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 2);
                    int ip3 = datagram.left(2).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 2);
                    int ip4 = datagram.left(2).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 2);
                    host.setAddress(QString::number(ip1) + "." + QString::number(ip2) + "." + QString::number(ip3) + "." + QString::number(ip4));
                    qDebug() << "IPv4 Address:" << host.address();

                } else if(datagram.startsWith("000c")){
                    qDebug() << "got a PTR record";
                    datagram = datagram.right(datagram.length() - 4);

                    // ignoring flags
                    datagram = datagram.right(datagram.length() - 12);

                    // ignoring rest
                    len = datagram.left(4).toInt(0, 16);
                    datagram = datagram.right(datagram.length() - 4);
                    qDebug() << "PTR:" << QString(QByteArray::fromHex(datagram.left(len * 2))) << "len:" << len;
                    datagram = datagram.right(datagram.length() - len * 2);
                }
            }
        }

        xDebug(XDAREA_DISCOVERY) << "Found host:" << host.hostname() << "IP:" << host.address() << "Port:" << host.port() << "XBMC-JSONRPC:" << host.xbmcJsonrpcSupported() << "XBMC-Web-Server:" << host.xbmcHttpSupported() << "MAC:" << host.hwAddr();

        if(!host.address().isEmpty()
                && host.port() != 0
                && host.xbmcJsonrpcSupported()
                && host.xbmcHttpSupported()) {

            Xbmc::instance()->hostModel()->insertOrUpdateHost(host);
        }

    }
}

bool XbmcDiscovery::setMulticastGroup(const QHostAddress &groupAddress, bool join)
{
    QNetworkInterface interface;
    int sockOpt = 0;
    void *sockArg;
    int sockArgSize;

    ip_mreq mreq4;
#ifndef QT_NO_IPV6
    ipv6_mreq mreq6;

    if (groupAddress.protocol() == QAbstractSocket::IPv6Protocol) {
        if(join) {
            sockOpt = IPV6_JOIN_GROUP;
        } else {
            sockOpt = IPV6_LEAVE_GROUP;
        }
        sockArg = &mreq6;
        sockArgSize = sizeof(mreq6);
        memset(&mreq6, 0, sizeof(mreq6));
        Q_IPV6ADDR ip6 = groupAddress.toIPv6Address();
        memcpy(&mreq6.ipv6mr_multiaddr, &ip6, sizeof(ip6));
        mreq6.ipv6mr_interface = interface.index();
    } else
#endif
        if (groupAddress.protocol() == QAbstractSocket::IPv4Protocol) {
            if(join) {
                sockOpt = IP_ADD_MEMBERSHIP;
            } else {
                sockOpt = IP_DROP_MEMBERSHIP;
            }
            sockArg = &mreq4;
            sockArgSize = sizeof(mreq4);
            memset(&mreq4, 0, sizeof(mreq4));
            mreq4.imr_multiaddr.s_addr = htonl(groupAddress.toIPv4Address());
            qDebug() << "set address to:" << groupAddress.toIPv4Address();

            if (interface.isValid()) {
                qDebug() << "interface is valid!";
                QList<QNetworkAddressEntry> addressEntries = interface.addressEntries();
                if (!addressEntries.isEmpty()) {
                    QHostAddress firstIP = addressEntries.first().ip();
                    mreq4.imr_interface.s_addr = htonl(firstIP.toIPv4Address());
                } else {
                    qDebug() << "error getting addressentries: network unreachable";
                    return false;
                }
            } else {
                mreq4.imr_interface.s_addr = INADDR_ANY;
            }
        } else {
            // unreachable
            qDebug() << "error network unreachable: Protocol unsupported";
            return false;
        }

    int res = setsockopt(m_socket->socketDescriptor(), IPPROTO_IP, sockOpt, sockArg, sockArgSize);
    if (res == -1) {
        switch (errno) {
        case ENOPROTOOPT:
            qDebug() << "Error setting sockopt: Unsupported";
            break;
        default:
            qDebug() << "Error setting sockopt: Unknown Socket";
            break;
        }
        return false;
    }
    return true;
}

QString XbmcDiscovery::extractName(const QByteArray datagram, int index)
{
    QString name;
    index *=2;
    qDebug() << "***" << datagram[index] << datagram[index+1] << datagram[index+2] << datagram[index+3] << datagram[index+4] << datagram[index+5] << datagram[index+6] << datagram[index+7];
    while (!(datagram[index] == '0' && datagram[index+1] == '0') && !(datagram[index] == 'c' && datagram[index+1] == '0')) {
        name.append(QString(QByteArray::fromHex(datagram.right(datagram.length() - index).left(2))));
        index += 2;
    }
    if (datagram[index] == 'c' && datagram[index+1] == '0') {
        index += 2;
        int newIndex = datagram.right(datagram.length() - index).left(2).toInt(0, 16);
        name.append(extractName(datagram, newIndex));
    }
    return name;
}

void XbmcDiscovery::setContinuousDiscovery(bool cd)
{
    if(cd) {
        m_continuousDiscoveryTimer.start(3000);
    } else {
        m_continuousDiscoveryTimer.stop();
    }
}
