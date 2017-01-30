#ifndef INTERVALLEGEND_H
#define INTERVALLEGEND_H

#include <map>
#include <algorithm>
#include <QtGui/QColor>
#include "legend.h"

namespace noise {
    namespace utils {
        class IntervalLegend : public Legend {

        public:
            IntervalLegend ();
            IntervalLegend (const IntervalLegend& other);
            virtual ~IntervalLegend ();
            virtual QColor lookup (const double& value) override;
            void addEntry (const double& value, const QColor& colour) override;
            unsigned removeEntries (const double& from, const double& unto);
            bool isValid() override;
        protected:
            std::map<double, QColor> intervals;
        };
    } // utils
} // noise
#endif // INTERVALLEGEND_H