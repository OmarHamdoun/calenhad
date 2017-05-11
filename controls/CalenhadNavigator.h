//
// Created by martin on 08/05/17.
//

#ifndef CALENHAD_CALENHADNAVIGATOR_H
#define CALENHAD_CALENHADNAVIGATOR_H


#include <QtWidgets/QWidget>
#include <QtWidgets/QPushButton>
#include <qwt/qwt_compass.h>

class QwtCompass;
class CalenhadNavigatorNeedle;

class NavigationEvent {
public:
    NavigationEvent (const double& azimuth, const double& distance);
    virtual ~NavigationEvent();

    double azimuth()const;
    double distance()const;

protected:
    double _azimuth;
    double _distance;
};

class CalenhadNavigator : public QwtCompass {
Q_OBJECT
public:
    CalenhadNavigator (QWidget* parent);

    virtual ~CalenhadNavigator();
    double length();

    void paintEvent (QPaintEvent* e) override;
    void mouseMoveEvent (QMouseEvent* e) override;
    void mouseReleaseEvent (QMouseEvent* e);

signals:
    void navigationRequested (const NavigationEvent&);

protected:

    QPointF _mousePos;
    bool _active;
    double _length;
    QPixmap* _buffer;


};


#endif //CALENHAD_CALENHADNAVIGATOR_H