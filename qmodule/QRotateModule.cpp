//
// Created by martin on 26/11/16.
//

#include <libnoise/module/modulebase.h>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QDoubleSpinBox>
#include "QRotateModule.h"
#include "../pipeline/ModuleFactory.h"
#include "QNode.h"

QRotateModule::QRotateModule (noise::module::RotatePoint* m, QWidget* parent) : QModule (m, parent) {

}

QRotateModule::~QRotateModule() {

}

void QRotateModule::initialise() {
    QModule::initialise();
    _name = "New Rotation";

    xAngleSpin = angleParameterControl ("Rotate X");
    connect (xAngleSpin, SIGNAL (valueChanged (double)), this, SLOT (setXAngle (double)));
    _contentLayout -> addRow (tr ("around X"), xAngleSpin);
    yAngleSpin = angleParameterControl ("Rotate Y");
    connect (yAngleSpin, SIGNAL (valueChanged (double)), this, SLOT (setYAngle (double)));
    _contentLayout -> addRow (tr ("around Y"), yAngleSpin);
    zAngleSpin = angleParameterControl ("Rotate Z");
    _contentLayout -> addRow (tr ("around Z"), zAngleSpin);
    _isInitialised = true;
    emit nodeChanged ("initialised", 0);

}

double QRotateModule::xAngle() {
    return module() -> GetXAngle();
}

double QRotateModule::yAngle() {
    return module() -> GetYAngle();
}

double QRotateModule::zAngle() {
    return module() -> GetZAngle();
}


void QRotateModule::setXAngle (double value) {
    module() -> SetXAngle (value);
    emit (nodeChanged ("xAngle", value));
    xAngleSpin -> setValue (value);
}

void QRotateModule::setYAngle (double value) {
    module() -> SetYAngle (value);
    emit (nodeChanged ("yAngle", value));
    yAngleSpin -> setValue (value);
}

void QRotateModule::setZAngle (double value) {
    module() -> SetZAngle (value);
    emit (nodeChanged ("zAngle", value));
    zAngleSpin -> setValue (value);
    return;
}

RotatePoint* QRotateModule::module () {
    RotatePoint* r = dynamic_cast<RotatePoint*> (_module);
    return r;
}

ModuleType QRotateModule::type() {
    return ModuleType::ROTATE;
}

QRotateModule* QRotateModule::newInstance () {
    RotatePoint* m = new RotatePoint();
    QRotateModule* qm = new QRotateModule (m);
    qm -> initialise();
    return qm;
}

QRotateModule* QRotateModule::addCopy (CalenhadModel* model){
    QRotateModule* qm = QRotateModule::newInstance();
    if (qm) {
        qm -> setModel (model);
        qm -> setXAngle (xAngle());
        qm -> setYAngle (yAngle());
        qm -> setZAngle (zAngle());
    }
    return qm;
}

QString QRotateModule::typeString () {
    return "Rotate";
}