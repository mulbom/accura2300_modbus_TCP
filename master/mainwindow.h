#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QVector>
#include <QTimer>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_panner.h>

namespace Ui { class MainWindow; }

struct Mbap { quint16 tid; quint16 pid; quint16 len; quint8 uid; };

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    bool connection = false;
    int nApply = 0;
    QVector<float> floats;
    ~MainWindow();

private slots:
    void on_addr_toggled(bool checked);
    void on_connect_clicked();
    void on_apply_clicked();
    void onSockReadyRead();
    void onAutoApplyTimeout();

    void on_stop_clicked();

private:
    Ui::MainWindow *ui;
    QTcpSocket* m_sock;
    QByteArray m_cliBuf;
    QTimer *m_autoTimer;
    QwtPlot *plot;
    QwtPlotCurve *curve[4];
    QwtPlotPanner *panner;
    QVector<double> yData[4];
    QVector<double> xData[4];

    bool parseInputs(QString &ip, quint16 &port, int &timeoutMs, QString &err);
    static bool takeOneModbusTcpFrame(QByteArray& buf, QByteArray& outFrame);
    static bool parseModbusTcpFrame(const QByteArray& frame, Mbap& mb, quint8& fc, QByteArray& pdu);
    static QVector<quint16> parseModbus03Reply(const QByteArray& rx, QString* err);
    static QByteArray buildModbusReadReq(quint16 transId, quint8 unitId, quint16 startAddr, quint16 regCount);
    static QByteArray buildModbusMultiReadReq(quint16 transId, quint8 unitId, QStringList Addrs, quint16 regCount);
    void sendModbusReq();
    void addPoint(double x, double y, int nReg);
    void onAddValue(double x, double y, int nReg);
};

#endif
