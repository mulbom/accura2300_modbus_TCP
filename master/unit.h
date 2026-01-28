#ifndef UNIT_H
#define UNIT_H

#include <QtNetwork/QTcpSocket>
#include <QtCore/QDataStream>
#include <QtCore/QScopedPointer>
class unit
{
private:
    QTcpSocket sock_;
    quint16 txid_;
    int timeoutMs_;
public:
    unit();
    struct Voltage_I
    {
        double a;
        double b;
        double c;
        double avg;
    };

    typedef struct Voltage_LN // 전압
    {
        Voltage_I i;
    } VLN;

    typedef struct Voltage_LL // 선간전압
    {
        Voltage_I i;
    } VLL;

    typedef struct Voltage_FDMT // 기본파 전압 평균
    {
        Voltage_I fdmt;
    } VFDMT;

    typedef struct Voltage_THD // 고조파 왜곡율
    {
        Voltage_I THD;
    } VTHD;

    typedef struct Voltage_Phasor // 축 위치
    {
        double a_x;
        double a_y;
        double b_x;
        double b_y;
        double c_x;
        double c_y;
    } VPHASOR;

    typedef struct Voltage_Unbalance // 불평형율
    {
        double LN_ub;
        double LL_ub;
        double U0_ub;
        double U2_ub;
    } VUB;

    typedef struct MeasureData
    {
        VLN vln;
        VLL vll;
        VFDMT vfdmt;
        VTHD vthd;
        VUB vub;
        VPHASOR vphasor;
        double Frequency; // 입력 전압 주파수
    } PT3Data;
};

#endif // UNIT_H
