#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QAbstractSocket>
#include <QAbstractTableModel>
#include <QHostAddress>
#include <QtEndian>
#include <QDataStream>
#include <cstring>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <qwt_legend.h>
#include <qwt_plot_grid.h>

static inline quint16 rd16be(const uchar* p){ return quint16((p[0] << 8) | p[1]); }

int applyCnt = 0;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
     ui(new Ui::MainWindow),
     plot(nullptr),
     panner(nullptr),
    m_sock(new QTcpSocket(this))
{
    ui->setupUi(this);

    setWindowTitle(tr("fdc_test"));
    m_autoTimer = new QTimer(this);

    connect(m_autoTimer, SIGNAL(timeout()), this, SLOT(on_apply_clicked()));

    plot = new QwtPlot(ui->widget);
    plot->setTitle("RESPONE");
    plot->setCanvasBackground(Qt::white);
    plot->setAxisTitle(QwtPlot::xBottom, "TIME");
    plot->setAxisTitle(QwtPlot::yLeft, "VAL");

    plot->setGeometry(ui->widget->rect());
    plot->setContentsMargins(0,0,0,0);
    plot->setAutoFillBackground(true);
    plot->setSizePolicy(ui->widget->sizePolicy());

    QwtPlotGrid *grid = new QwtPlotGrid();
    grid->attach(plot);

    for (int i = 0; i < 5; ++i) {
        curve[i] = new QwtPlotCurve(QString("Reg %1").arg(i));
        curve[i]->setRenderHint(QwtPlotItem::RenderAntialiased);
        curve[i]->setStyle(QwtPlotCurve::Lines);
        curve[i]->setBrush(Qt::NoBrush);
        curve[i]->attach(plot);
    }
    curve[0]->setPen(QPen(Qt::red, 2));
    curve[1]->setPen(QPen(Qt::blue, 2));
    curve[2]->setPen(QPen(Qt::green, 2));
    curve[3]->setPen(QPen(Qt::yellow, 2));
    curve[4]->setPen(QPen(Qt::magenta, 2));

    panner = new QwtPlotPanner(plot->canvas());
    panner->setMouseButton(Qt::LeftButton);
    panner->setOrientations(Qt::Horizontal);

    plot->setAxisScale(QwtPlot::xBottom, 0.0, 10.0);
    plot->setAxisScale(QwtPlot::yLeft, 0.0, 240.0);
}

MainWindow::~MainWindow()
{
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
    if (ipText.isEmpty() || portText.isEmpty() || timeoutText.isEmpty())
        return false;
    QHostAddress addr;
    if (!addr.setAddress(ipText)) return false;
    bool okPort = false; int portInt = portText.toInt(&okPort);
    if (!okPort || portInt < 1 || portInt > 65535)
    {
        err = "1~65535";
        return false;
    }
    bool okTimeout = false;
    int t = timeoutText.toInt(&okTimeout);
    if (!okTimeout || t <= 0)
        return false;
    ip = ipText; port = static_cast<quint16>(portInt); timeoutMs = t;
    return true;
}

void MainWindow::on_connect_clicked()
{
    QString ip; quint16 port = 0; int timeoutMs = 0; QString err;
    if (!parseInputs(ip, port, timeoutMs, err))
    {
        ui->label->setText("no connection");
        return;
    }
    if (m_sock->state() != QAbstractSocket::UnconnectedState)
        m_sock->abort();
    m_sock->connectToHost(ip, port);
    if (!m_sock->waitForConnected(timeoutMs))
    {
        const QString em = m_sock->errorString();
        QMessageBox::critical(this, "Connect Fail", QString("server(%1:%2) connect fail : %3").arg(ip).arg(port).arg(em));
        ui->label->setText("no connection");
        return;
    }
    QMessageBox::information(this, "Connected", QString("server(%1:%2) connected").arg(ip).arg(port));
    ui->label->setText("connected");
    connect(m_sock, SIGNAL(readyRead()), this, SLOT(onSockReadyRead()));
}

bool MainWindow::takeOneModbusTcpFrame(QByteArray &buf, QByteArray &outFrame)
{
    if (buf.size() < 7) return false;
    const uchar* p = reinterpret_cast<const uchar*>(buf.constData());
    const quint16 pid = qFromBigEndian<quint16>(p + 2);
    const quint16 len = qFromBigEndian<quint16>(p + 4);

    if (pid != 0x0000)
    {
        buf.remove(0, 1);
        return false;
    }
    if (len < 2)
    {
        buf.remove(0, 1);
        return false;
    }

    const int total = 6 + (int)len;
    const int MAX_FRAME = 4096;
    if (total > MAX_FRAME)
    {
        buf.remove(0, 1);
        return false;
    }
    if (buf.size() < total)
        return false;

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

QByteArray MainWindow::buildModbusReadReq(quint16 transId, quint8 unitId, quint16 startAddr, quint16 regCount)
{
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << transId;
    out << quint16(0x0000);
    out << quint16(6);
    out << unitId;
    out << quint8(0x03); //funcCode
    out << startAddr;
    out << regCount;
    return frame;
}

QByteArray MainWindow::buildModbusMultiReadReq(quint16 transId, quint8 unitId, QStringList Addrs, quint16 regCount)
{
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    quint8 functionCode = 0x65;
    quint8 numBlocks    = Addrs.size();
    out << transId;
    out << quint16(0x0000);
    quint16 pduLength = 1 + 1 + 1 + (numBlocks * 4);
    out << quint16(pduLength);
    out << unitId;
    out << functionCode;
    out << numBlocks;

    for (int i = 0; i < Addrs.size(); i++)
    {
        bool ok = false;
        quint16 mapAddr = Addrs[i].toUShort(&ok, 10);
        if (!ok) continue;

        quint16 startAddr = (mapAddr > 0) ? (mapAddr - 1) : 0;
        out << startAddr;
        out << regCount;
    }



    return frame;
}

static inline float pairToFloat(quint16 hi, quint16 lo, bool swap)
{
    quint32 u = swap ? ((quint32(lo) << 16) | hi) : ((quint32(hi) << 16) | lo);
    float f;
    memcpy(&f, &u, sizeof(float));
    return f;
}

void MainWindow::sendModbusReq()
{
    bool ok = false;
    QString strAddrs= ui->start_addr->text().trimmed();
    QStringList Addrs = strAddrs.split(",");
    if(Addrs.size() <= 1)
    {
        quint16 mapAddr = ui->start_addr->text().toUShort(&ok, 10);
        if (!ok)
        {
            QMessageBox::warning(this, "entering", "address");
            return;
        }
        quint16 startAddr = (mapAddr > 0) ? (mapAddr - 1) : 0;
        quint16 transactionId = 0x0013;
        quint8  unitId        = 1;
        quint16 regCount      = 2;
        QByteArray baseline = buildModbusReadReq(transactionId, unitId, startAddr, regCount);
        //log
        QByteArray hex = baseline.toHex().toUpper();
        QString spacedHex;
        for (int i = 0; i < hex.size(); i += 2) {
            if (i > 0) spacedHex += " ";
            spacedHex += hex.mid(i, 2);
        }
        ui->signLog->append(QString("Modbus Req : %1\n").arg(spacedHex));

        QByteArray req = baseline;
        if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) { QMessageBox::warning(this, "network", "not connected"); return; }
        m_sock->write(req);
        m_sock->flush();
    }
    else
    {
        quint16 transactionId = 0x0013;
        quint8  unitId = 1;
        quint16 regCount = 2;
        QByteArray baseline = buildModbusMultiReadReq(transactionId, unitId, Addrs, regCount);
        //log
        QByteArray hex = baseline.toHex().toUpper();
        QString spacedHex;
        for (int i = 0; i < hex.size(); i += 2)
        {
            if (i > 0) spacedHex += " ";
            spacedHex += hex.mid(i, 2);
        }
        ui->signLog->append(QString("Modbus Req : %1\n").arg(spacedHex));

        QByteArray req = baseline;
        if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState)
        {
            QMessageBox::warning(this, "network", "not connected");
            return;
        }
        m_sock->write(req);
        m_sock->flush();
    }
}

void MainWindow::on_apply_clicked()
{
    nApply = 0;
    floats.clear();
    if (ui->parsetest_tableWidget->rowCount() != ui->lenth->text().toInt())
    {
        ui->parsetest_tableWidget->setColumnCount(1);
        ui->parsetest_tableWidget->setRowCount(ui->lenth->text().toInt());
    }
    if(!m_autoTimer->isActive())
        m_autoTimer->start(3000);
    sendModbusReq();
}

void MainWindow::onSockReadyRead()
{
    m_cliBuf.append(m_sock->readAll());
    QByteArray frame;
    float avgV;
    quint16 fir;
    quint16 sec;
    while (takeOneModbusTcpFrame(m_cliBuf, frame))
    {
        Mbap mb;
        quint8 fc = 0;
        QByteArray pdu;
        //log
        QByteArray hex = frame.toHex().toUpper();
        QString spacedHex;
        for (int i = 0; i < hex.size(); i += 2)
        {
            if (i > 0) spacedHex += " ";
            spacedHex += hex.mid(i, 2);
        }
        ui->signLog->append(QString("Modbus Res : %1\n").arg(spacedHex));
        if (!parseModbusTcpFrame(frame, mb, fc, pdu)) continue;
        if (fc == 0x03)
        {
            if (pdu.size() < 1) continue;
            const quint8 byteCount = (quint8)pdu[0];
            if (pdu.size() != 1 + byteCount) continue;
            if (byteCount % 2 != 0) continue;
            const int nRegs = byteCount / 2;
            QVector<quint16> regs;
            regs.reserve(nRegs);
            const uchar* ptr = (const uchar*)pdu.constData() + 1;
            for (int i = 0; i < nRegs; ++i)
                regs.push_back(qFromBigEndian<quint16>(ptr + 2*i));
            const bool swapWords = false;
            QVector<float> floats;
            for (int i = 0; i + 1 < regs.size(); i += 2)
                floats.push_back(pairToFloat(regs[i], regs[i+1], swapWords));
            if (!floats.isEmpty()) // 11107
            {
                if (ui->start_addr->text().trimmed() == "11107")
                {

                    if (ui->apply_test)
                        ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3 | Vavg_ln=%4 V").arg(mb.tid).arg(mb.uid).arg(nRegs).arg(floats[0], 0, 'f', 3));
                    ui->label_v->setText(QString("Vavg_ln = %1 V").arg((floats[0]/applyCnt), 0, 'f', 3));
                    onAddValue((double)applyCnt, (double)floats[0], 0);
                }
                else
                {
                    if (ui->apply_test)
                        ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3 | Reg = %4 ").arg(mb.tid).arg(mb.uid).arg(nRegs).arg(floats[0], 0, 'f', 3));
                    ui->label_v->setText(QString("Reg = %1 ").arg((floats[0]/applyCnt), 0, 'f', 3));
                }
            }
            else
            {
                if (ui->apply_test) ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3").arg(mb.tid).arg(mb.uid).arg(nRegs));
            }
            auto setRegsToTable = [&](const QVector<quint16>& regsRef)
            {
                for (int r=0; r<(regsRef.size()); ++r)
                {
                    if (!ui->parsetest_tableWidget->item(r,0))
                        ui->parsetest_tableWidget->setItem(r,0,new QTableWidgetItem);
                    ui->parsetest_tableWidget->item(r,0)->setText(QString::number(regsRef[r]));
                }
            };
            setRegsToTable(regs);
        }
        else if (fc == 0x65)
        {
            if (pdu.size() < 1) continue;
            const quint8 byteCount = 4;
            if (byteCount % 2 != 0) continue;
            for(int nAddr = 0; nAddr < pdu.size(); nAddr+=4)
            {
                const int nRegs = byteCount / 2;
                QVector<quint16> regs;
                regs.reserve(nRegs);
                const uchar* ptr = (const uchar*)pdu.constData() + 1;
                for (int i = 0; i < nRegs; ++i)
                    regs.push_back(qFromBigEndian<quint16>(ptr + nAddr + 2*i));
                const bool swapWords = false;
                for (int i = 0; i + 1 < regs.size(); i += 2)
                    floats.push_back(pairToFloat(regs[i], regs[i+1], swapWords));

                if (!floats.isEmpty())
                {
                    qDebug() << "floats size : " << floats.size();
                    switch(floats.size())
                    {
                    case 2:
                        if (ui->apply_test)
                            ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3 | Reg1=%4 | Reg2 = %5")
                                                    .arg(mb.tid).arg(mb.uid).arg(floats.size()).arg(floats[0], 0, 'f', 3).arg(floats[1], 0, 'f', 3));
                        ui->label_v->setText(QString("Reg1 = %1 ").arg((floats[0]/applyCnt), 0, 'f', 3));
                        ui->label_a->setText(QString("Reg2 = %1 ").arg((floats[1]/applyCnt), 0, 'f', 3));
//                        for(int plots = 0; plots < floats.size(); plots++) { onAddValue((double)applyCnt, floats[plots], plots); }
                        break;
                    case 3:
                        if (ui->apply_test)
                            ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3 | Reg1=%4 | Reg2 = %5 | Reg3 = %6")
                                                    .arg(mb.tid).arg(mb.uid).arg(floats.size()).arg(floats[0], 0, 'f', 3).arg(floats[1], 0, 'f', 3).arg(floats[2], 0, 'f', 3));
                        ui->label_v->setText(QString("Reg1 = %1 ").arg((floats[0]/applyCnt), 0, 'f', 3));
                        ui->label_a->setText(QString("Reg2 = %1 ").arg((floats[1]/applyCnt), 0, 'f', 3));
                        ui->label_kw->setText(QString("Reg3 = %1 ").arg((floats[2]/applyCnt), 0, 'f', 3));
//                        for(int plots = 0; plots < floats.size(); plots++) { onAddValue((double)applyCnt, floats[plots], plots); }
                        break;
                    case 4:
                        if (ui->apply_test) // reg 11107,11201,11217,11225
                            ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3 | Vavg_ln=%4 V | Iavg = %5 A | kWtotal = %6 kW | kWh = %7")
                                                    .arg(mb.tid).arg(mb.uid).arg(floats.size()).arg(floats[0], 0, 'f', 3).arg(floats[1], 0, 'f', 3).arg(floats[2], 0, 'f', 3).arg(floats[3], 0, 'f', 3));
                        ui->label_v->setText(QString("Vavg_ln = %1 V").arg((floats[0]/applyCnt), 0, 'f', 3));
                        ui->label_a->setText(QString("Iavg = %1 A").arg((floats[1]/applyCnt), 0, 'f', 3));
                        ui->label_kw->setText(QString("kW = %1 kW").arg((floats[2]/applyCnt), 0, 'f', 3));
                        ui->label_kwh->setText(QString("kWh = %1 kWh").arg((floats[3]/applyCnt), 0, 'f', 3));
//                        for(int plots = 0; plots < floats.size(); plots++) { onAddValue((double)applyCnt, floats[plots], plots); }
                        break;
                    case 5:
                        if (ui->apply_test) // reg 11107,11201,11217,11225,11153
                            ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3 | Vavg_ln=%4 V | Iavg = %5 A | kWtotal = %6 kW | kWh = %7 | Temp = %8")
                                                    .arg(mb.tid).arg(mb.uid).arg(floats.size()).arg(floats[0], 0, 'f', 3).arg(floats[1], 0, 'f', 3).arg(floats[2], 0, 'f', 3).arg(floats[3], 0, 'f', 3).arg(floats[4], 0, 'f', 3));
                        ui->label_v->setText(QString("Vavg_ln = %1 V").arg((floats[0]), 0, 'f', 3));
                        ui->label_a->setText(QString("Iavg = %1 A").arg((floats[1]), 0, 'f', 3));
                        ui->label_kw->setText(QString("kW = %1 kW").arg((floats[2]), 0, 'f', 3));
                        ui->label_kwh->setText(QString("kWh = %1 kWh").arg((floats[3]), 0, 'f', 3));
                        ui->label_temp->setText(QString("temp = %1 `C").arg((floats[4]), 0, 'f', 3));
                        for(int plots = 0; plots < floats.size(); plots++) { onAddValue((double)applyCnt, floats[plots], plots); }
                        break;
                    }
                }
                else
                {
                    if (ui->apply_test) ui->apply_test->setText(QString("TID=%1 UID=%2 FC=03 | REGS=%3").arg(mb.tid).arg(mb.uid).arg(floats.size()));
                }

                auto setRegsToTable = [&](const QVector<quint16>& regsRef)
                {
                    for (int r= 0; r<(regsRef.size()); ++r)
                    {
                        if (!ui->parsetest_tableWidget->item(nApply,0))
                            ui->parsetest_tableWidget->setItem(nApply,0,new QTableWidgetItem);
                        ui->parsetest_tableWidget->item(nApply,0)->setText(QString::number(regsRef[r]));
                        nApply += 1;
                    }
                };
                setRegsToTable(regs);
            }
        }
        else
        {
            if (ui->apply_test) ui->apply_test->setText(QString("TID=%1 UID=%2 FC=0x%3 LEN=%4").arg(mb.tid).arg(mb.uid).arg(fc, 2, 10, QLatin1Char('0')).arg(pdu.size()));
        }
    }
}

void MainWindow::onAutoApplyTimeout()
{
    on_apply_clicked();
}

void MainWindow::on_stop_clicked()
{
    m_autoTimer->stop();
}

void MainWindow::addPoint(double x, double y, int nReg)
{
    if (nReg < 0 || nReg >= 4) return;
    if (!curve[nReg]) return;

    xData[nReg].append(x);
    yData[nReg].append(y);

    const int maxPoints = 100000;
    if (xData[nReg].size() > maxPoints) {
        int removeCnt = xData[nReg].size() - maxPoints;
        xData[nReg].remove(0, removeCnt);
        yData[nReg].remove(0, removeCnt);
    }

    int sz = xData[nReg].size();
    if (sz > 0) {
        curve[nReg]->setData(xData[nReg].constData(), yData[nReg].constData(), sz);
    } else {
        curve[nReg]->setData(nullptr, nullptr, 0);
    }

    plot->replot();
}


void MainWindow::onAddValue(double x, double y, int nReg)
{
    addPoint(x, y, nReg);

//    if (!xData[nReg].isEmpty()) {
//        double xmax = xData[nReg].last();
//        double xmin = xmax - 10.0;
//        plot->setAxisScale(QwtPlot::xBottom, xmin, xmax);
//    }
}
