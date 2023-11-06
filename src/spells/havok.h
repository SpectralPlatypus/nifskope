#ifndef HAVOK_H
#define HAVOK_H

#include <QDialog> // Inherited

#include <QPersistentModelIndex>

class NifModel;

class QWidget;
class QSpinBox;
class QComboBox;
class QDoubleSpinBox;
class QVBoxLayout;

//! String palette QRegularExpression dialog for spEditStringEntries
class VHacdDialog final : public QDialog
{
    Q_OBJECT

public:
    struct DialValues
    {
    public:
        enum FillMethod : uint8_t
        {
            FloodFill,
            Surface,
            Raycast,
        };
        uint32_t resolution;
        uint32_t maxConvexHulls;
        uint32_t maxNumVerticesPerCH;
        FillMethod fillMethod;
        double minimumVolumePercentErrorAllowed;

        int matlsIndex;

        DialValues(uint32_t res, uint32_t convexHulls, double error, uint32_t vpch, int matlsIdx, FillMethod fill = FillMethod::FloodFill) :
            resolution(res),
            maxConvexHulls(convexHulls),
            maxNumVerticesPerCH(vpch),
            fillMethod(fill),
            minimumVolumePercentErrorAllowed(error),
            matlsIndex(matlsIdx)
        {
        }
    };

    //! Constructor. Sets widgets and layout.
    VHacdDialog(QWidget * parent = nullptr );

protected:
    //! Resolution
    QSpinBox * paramRes;
    //! Min Volume Percent Error Allowed
    QDoubleSpinBox * paramErr;
    //! Max number of convex hulls to produce
    QSpinBox* paramMaxch;
    //! Max number of vertices per convex hull
    QSpinBox* paramVpch;
    //! Lists fill methods
    QComboBox* paramFill;
    //! Lists the strings in the palette
    QComboBox * paramMatls;

public slots:
    //! Set the string palette entries
    void setParams( const DialValues values );
    //! Get the modified string palette
    DialValues getParams();
};

#endif // HAVOK_H
