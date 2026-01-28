#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QAbstractSocket>
#include <QtEndian>
#include <QDataStream>
#include <cstring>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_server(new QTcpServer(this))
{
    ui->setupUi(this);
    fillSlaveTable();
    connect(m_server, SIGNAL(newConnection()), this, SLOT(onServerNewConnection()));
}

MainWindow::~MainWindow()
{
    stopSlave();
    delete ui;
}

void MainWindow::on_addr_toggled(bool checked)
{
    ui->ip->setReadOnly(checked);
}

bool MainWindow::parseInputs(QString &ip, quint16 &port, int &timeoutMs, QString &err)
{
    err.clear();
    const QString ipText = ui->ip->text().trimmed();
    const QString portText = ui->port->text().trimmed();
    const QString timeoutText = ui->timeout->text().trimmed();
    if (ipText.isEmpty() || portText.isEmpty() || timeoutText.isEmpty()) return false;
    QHostAddress addr;
    if (!addr.setAddress(ipText)) return false;
    bool okPort = false;
    int portInt = portText.toInt(&okPort);
    if (!okPort || portInt < 1 || portInt > 65535) { err = "1~65535"; return false; }
    bool okTimeout = false; int t = timeoutText.toInt(&okTimeout);
    if (!okTimeout || t <= 0) return false;
    ip = ipText; port = static_cast<quint16>(portInt); timeoutMs = t;
    return true;
}

bool MainWindow::startSlave(const QString& ip, quint16 port, QString& err)
{
    err.clear();
    if (m_server->isListening()) { err = "already listening"; return false; }
    QHostAddress bindAddr;
    if (ip == "0.0.0.0" || ip.trimmed().isEmpty()) bindAddr = QHostAddress::Any;
    else if (!bindAddr.setAddress(ip)) { err = "binding IP error"; return false; }
    if (!m_server->listen(bindAddr, port)) { err = QString("listen fail : %1").arg(m_server->errorString()); return false; }
    return true;
}

void MainWindow::stopSlave()
{
    foreach (QTcpSocket* s, m_clients) {
        if (!s) continue;
        s->disconnect(this);
        s->disconnectFromHost();
        s->deleteLater();
        m_srvBuf.remove(s);
    }
    m_clients.clear();
    if (m_server->isListening()) {
        m_server->close();
        isConnecting();
    }
}

void MainWindow::on_listen_clicked()
{
    QString ip; quint16 port = 0; int timeoutMs = 0; QString err;
    if (!parseInputs(ip, port, timeoutMs, err)) {
        if (!err.isEmpty()) QMessageBox::warning(this, "entering error", err);
        return;
    }
    if (!startSlave(ip, port, err)) { QMessageBox::critical(this, "no connection", err); return; }
    QMessageBox::information(this, "Slave start", QString("server(slave) %1:%2 listening").arg(ip).arg(port));
    isConnecting();
}

void MainWindow::on_stopListen_clicked()
{
    stopSlave();
    QMessageBox::information(this, "Slave stop", "server stop");
}

void MainWindow::onServerNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* s = m_server->nextPendingConnection();
        m_clients << s;
        connect(s, SIGNAL(readyRead()), this, SLOT(onClientReadyRead()));
        connect(s, SIGNAL(disconnected()), this, SLOT(onClientDisconnected()));
    }
}

bool MainWindow::takeOneModbusTcpFrame(QByteArray &buf, QByteArray &outFrame)
{
    if (buf.size() < 7) return false;
    const uchar* p = reinterpret_cast<const uchar*>(buf.constData());
    const quint16 pid = qFromBigEndian<quint16>(p + 2);
    const quint16 len = qFromBigEndian<quint16>(p + 4);
    if (pid != 0x0000) { buf.remove(0, 1); return false; }
    const int total = 6 + len;
    if (total <= 6 || total > 260) { buf.remove(0, 1); return false; }
    if (buf.size() < total) return false;
    outFrame = buf.left(total);
    buf.remove(0, total);
    return true;
}

bool MainWindow::parseModbusTcpFrame(const QByteArray& frame, Mbap& mb, quint8& fc, QByteArray& pdu)
{
    if (frame.size() < 8) return false;
    const uchar* p = reinterpret_cast<const uchar*>(frame.constData());
    mb.tid = qFromBigEndian<quint16>(p + 0);
    mb.pid = qFromBigEndian<quint16>(p + 2);
    mb.len = qFromBigEndian<quint16>(p + 4);
    mb.uid = *(p + 6);

    if (mb.pid != 0x0000) return false;
    if (mb.len < 2) return false;

    fc  = *(p + 7); // funccode
    if(fc == 0x03) // 03
        pdu = frame.mid(8);
    else if(fc == 0x65) // 101
    {
        pdu = frame.mid(8, 1);
        switch(frame.size())
        {
        case 25:
            pdu.append(frame.mid(17));
            break;
        case 33:
            pdu.append(frame.mid(21));
            break;
        case 41:
            pdu.append(frame.mid(25));
            break;
        case 49:
            pdu.append(frame.mid(29));
            break;
        }
    }
    applyCnt++;
    return true;
}

QByteArray MainWindow::buildModbus03Reply(quint16 transId, quint8 unitId, const QVector<quint16>& regs)
{
    const quint8 fc = 0x03;
    const quint8 byteCount = quint8(regs.size() * 2);
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    quint16 len = 1 + 1 + 1 + byteCount;
    out << transId;
    out << quint16(0x0000);
    out << len;
    out << unitId;
    out << fc;
    out << byteCount;
    for (quint16 v : regs) out << v;
    return frame;
}

QByteArray MainWindow::buildModbus65Reply(quint16 transId, quint8 unitId, const QVector<quint16>& regs)
{
    const quint8 fc = 0x65;
    const quint8 byteCount = quint8(regs.size() * 2);
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    quint16 len = 1 + 1 + 1 + byteCount;
    out << transId;
    out << quint16(0x0000);
    out << len;
    out << unitId;
    out << fc;
    out << byteCount;
    for (quint16 v : regs) out << v;
    return frame;
}

void MainWindow::onClientReadyRead()
{
    QTcpSocket* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    m_srvBuf[s].append(s->readAll());
    QByteArray frame;
    while (takeOneModbusTcpFrame(m_srvBuf[s], frame)) {
        Mbap mb; quint8 fc=0; QByteArray pdu;
        if (!parseModbusTcpFrame(frame, mb, fc, pdu)) continue;
        if (fc == 0x03)
        {
            if (pdu.size() < 4) continue;
            const uchar* pp = reinterpret_cast<const uchar*>(pdu.constData());
            quint16 startAddr = qFromBigEndian<quint16>(pp + 0);
            quint16 regCount  = qFromBigEndian<quint16>(pp + 2);
            QVector<quint16> regs; regs.reserve(regCount);
            for (int i = 0; i < regCount; ++i) {
                int row = startAddr + i;
                quint16 v = 0;
                if (row < ui->parsetest_tableWidget->rowCount() && ui->parsetest_tableWidget->item(row,0)) {
                    bool ok=false;
                    v = ui->parsetest_tableWidget->item(row,0)->text().toUShort(&ok, 0);
                    if (!ok) v = 0;
                }
                regs.push_back(v);
            }
            QByteArray resp = buildModbus03Reply(mb.tid, mb.uid, regs);
            s->write(resp);
            s->flush();
        }
        if (fc == 0x65)
        {
            if (pdu.size() < 4) continue;
            const uchar* pp = reinterpret_cast<const uchar*>(pdu.constData());
            quint16 startAddr = qFromBigEndian<quint16>(pp + 0);
            quint16 regCount  = qFromBigEndian<quint16>(pp + 2);
            QVector<quint16> regs; regs.reserve(regCount);
            for (int i = 0; i < regCount; ++i) {
                int row = startAddr + i;
                quint16 v = 0;
                if (row < ui->parsetest_tableWidget->rowCount() && ui->parsetest_tableWidget->item(row,0)) {
                    bool ok=false;
                    v = ui->parsetest_tableWidget->item(row,0)->text().toUShort(&ok, 0);
                    if (!ok) v = 0;
                }
                regs.push_back(v);
            }
            QByteArray resp = buildModbus03Reply(mb.tid, mb.uid, regs);
            s->write(resp);
            s->flush();
        }
    }
}

void MainWindow::onClientDisconnected()
{
    QTcpSocket* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    m_clients.removeAll(s);
    m_srvBuf.remove(s);
    s->deleteLater();
}

void MainWindow::isConnecting()
{
    if(m_server->isListening())
        ui->connected->setText("listening");
    else
        ui->connected->setText("no connection");
}
void MainWindow::fillSlaveTable()
{
    ui->parsetest_tableWidget->setColumnCount(1);
    ui->parsetest_tableWidget->setRowCount(11240);
    const quint16 demo[] = {1,1,0,0,0,0,0,0,0,0};
    for (int i=0; i<10; ++i) {
        const QString text = (i < (int)(sizeof(demo)/2)) ? QString::number(demo[i]) : "0";
        if (!ui->parsetest_tableWidget->item(i,0))
            ui->parsetest_tableWidget->setItem(i,0,new QTableWidgetItem);
        ui->parsetest_tableWidget->item(i,0)->setText(text);
    }
}
