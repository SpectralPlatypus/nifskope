#ifndef HAVOK_H
#define HAVOK_H

#include <QDialog> // Inherited
#include <QPersistentModelIndex>
#include "v-hacd/src/VHACD_Lib/public/VHACD.h"


//! \file havok.h VHacdDialog

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
        VHACD::IVHACD::Parameters params;
        int matlsIndex;

        DialValues(int res, double con, int vpch, int matlsIdx) :
            params(),
            matlsIndex(matlsIdx)
        {
            params.m_resolution = res;
            params.m_concavity = con;
            params.m_planeDownsampling = 4;
            params.m_convexhullDownsampling = 8;
            params.m_maxNumVerticesPerCH = vpch;
        }
    };
    //! Constructor. Sets widgets and layout.
    VHacdDialog(QWidget * parent = nullptr );

protected:
    //! Resolution
    QSpinBox * paramRes;
    //! Model used
    QDoubleSpinBox * paramConcav;
    //! Index of the string palette
    QSpinBox* paramVpch;
    //! Lists the strings in the palette
    QComboBox * paramMatls;

public slots:
    //! Set the string palette entries
    void setParams( const DialValues values );
    //! Get the modified string palette
    DialValues getParams();
};

#endif // HAVOK_H
