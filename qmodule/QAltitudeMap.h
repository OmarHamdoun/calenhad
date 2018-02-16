//
// Created by martin on 31/03/17.
//

#ifndef CALENHAD_QALTITUDEMAP_H
#define CALENHAD_QALTITUDEMAP_H

#include <QtCore/QString>
#include <QtXml/QDomElement>
#include "QModule.h"
#include "../controls/altitudemap/AltitudeMapConstants.h"



namespace calenhad {
    namespace mapping {
        class Curve;
    }
    namespace controls {
        namespace altitudemap {
            class AltitudeMapEditor;
            class AltitudeMapping;
        }
    }
    namespace qmodule {


        class QAltitudeMap : public QModule {
        Q_OBJECT
        public:

            QAltitudeMap (QWidget* parent = 0);

            virtual ~QAltitudeMap ();

            void makeContentPanel();

            QAltitudeMap* clone () override;

            QVector<calenhad::controls::altitudemap::AltitudeMapping> entries() const;

            virtual void inflate (const QDomElement& element) override;

            virtual void serialize (QDomDocument& doc) override;

            void addEntry (const double& in, const double& out = 0);

            QString curveFunction ();

            bool isFunctionInverted ();

        public slots:

            void updateEntries ();

            void editingFinished ();

            void resetMap ();

            void editAltitudeMap ();

        protected:
            calenhad::controls::altitudemap::AltitudeMapEditor* _editor;
            QMap<calenhad::controls::altitudemap::CurveType, calenhad::mapping::Curve*> _curves;
            void clearMap ();

            QString _oldXml;

            void preserve ();

            mapping::Curve* _curve;

            void initialise() override;
        };
    }
}


#endif //CALENHAD_QALTITUDEMAP_H
