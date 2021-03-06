//
// Created by martin on 11/11/16.
//

#ifndef CALENHAD_MODULEFACTORY_H
#define CALENHAD_MODULEFACTORY_H

#include <QString>
#include <QObject>
#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QFormLayout>
#include <QtWidgets/QDoubleSpinBox>
#include <QtCore/QSet>
#include <QtXml/QDomNode>
#include <qmodule/ParamValidator.h>


namespace calenhad {
    namespace qmodule {
        class Node;
    }

    namespace pipeline {
        class CalenhadModel;

        class ModuleFactory : public QObject {
        Q_OBJECT



            Q_ENUMS (ModuleType)
        public:
            ModuleFactory ();

            ~ModuleFactory ();
            QStringList types ();
            calenhad::qmodule::Node* createModule (const QString& moduleType, CalenhadModel* model);

            int seed ();

            QPixmap* getIcon (const QString& type);



            qmodule::Node* clone (qmodule::Node* other);

            QDomElement xml (const QString& type);
            QStringList paramNames();
            QString label (const QString& type);
            QString description (const QString& type);
            QString glsl (const QString& type);
            QSizeF scale (const QString& type);
        signals:

            void seedChanged (const int& seed);


        private:
            int _seed = 0;
            QMap<QString, QPixmap*> _icons;
            QStringList _paramNames;
            QMap<QString, QDomElement> _types;
            void initialise();
            qmodule::Node* inflateModule (const QString& type, CalenhadModel* model);

            QMap<QString, QString> _moduleLabels;
            QMap<QString, QString> _moduleDescriptions;
            QMap<QString, QString> _moduleCodes;
            QMap<QString, QSizeF> _moduleScales;
            qmodule::ParamValidator* validator (const QString& validatorType);

        };
    }
}

#endif //CALENHAD_MODULEFACTORY_H
