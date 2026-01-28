#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QVector>
#include <QHash>

namespace Ui { class MainWindow; }

struct Mbap { quint16 tid; quint16 pid; quint16 len; quint8 uid; };

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_addr_toggled(bool checked);
    void on_listen_clicked();
    void on_stopListen_clicked();
    void onServerNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    Ui::MainWindow *ui;
    QTcpServer* m_server;
    QList<QTcpSocket*> m_clients;
    QHash<QTcpSocket*, QByteArray> m_srvBuf;

    bool parseInputs(QString &ip, quint16 &port, int &timeoutMs, QString &err);
    bool startSlave(const QString& ip, quint16 port, QString& err);
    void stopSlave();
    void isConnecting();

    static bool takeOneModbusTcpFrame(QByteArray& buf, QByteArray& outFrame);
    static bool parseModbusTcpFrame(const QByteArray& frame, Mbap& mb, quint8& fc, QByteArray& pdu);
    static QByteArray buildModbus03Reply(quint16 transId, quint8 unitId, const QVector<quint16>& regs);
    void fillSlaveTable();
};

#endif
