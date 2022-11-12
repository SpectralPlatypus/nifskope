#include "spellbook.h"

#include "ui/widgets/colorwheel.h"
#include "ui/widgets/floatslider.h"

#include <QDialog>
#include <QSpinBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>

// Brief description is deliberately not autolinked to class Spell
/*! \file color.cpp
 * \brief Color editing spells (spChooseColor)
 *
 * All classes here inherit from the Spell class.
 */

//! Choose a color using a ColorWheel
class spChooseColor final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "Choose" ); }
    QString page() const override final { return Spell::tr( "Color" ); }
    QIcon icon() const override final { return ColorWheel::getIcon(); }
    bool instant() const override final { return true; }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        return nif->getValue( index ).isColor();
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        auto typ = nif->getValue( index ).type();
        if ( typ == NifValue::tColor3 ) {
            nif->set<Color3>( index, ColorWheel::choose( nif->get<Color3>( index ) ) );
        } else if ( typ == NifValue::tColor4 ) {
            nif->set<Color4>( index, ColorWheel::choose( nif->get<Color4>( index ) ) );
        } else if ( typ == NifValue::tByteColor4 ) {
            auto col = ColorWheel::choose( nif->get<ByteColor4>( index ) );
            nif->set<ByteColor4>( index, *static_cast<ByteColor4 *>(&col) );
        }


        return index;
    }
};

REGISTER_SPELL( spChooseColor )

//! Set an array of Colors
class spSetAllColor final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "Set All" ); }
    QString page() const override final { return Spell::tr( "Color" ); }
    QIcon icon() const override final { return ColorWheel::getIcon(); }
    bool instant() const override final { return true; }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        return nif->isArray( index ) && nif->getValue( index.child( 0, 0 ) ).isColor();
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        QModelIndex colorIdx = (nif->isArray( index )) ? index.child( 0, 0 ) : index;

        auto typ = nif->getValue( colorIdx ).type();
        if ( typ == NifValue::tColor3 )
            nif->setArray<Color3>( index, ColorWheel::choose( nif->get<Color3>( colorIdx ) ) );
        else if ( typ == NifValue::tColor4 )
            nif->setArray<Color4>( index, ColorWheel::choose( nif->get<Color4>( colorIdx ) ) );

        return index;
    }
};

REGISTER_SPELL( spSetAllColor )

bool getColorDialogue(int maxIdx, bool alphaEnable, int& retIdx, QColor& pickColor)
{
    /* those will be filled with the CVS data */

    // ask for precision
    QDialog dlg;
    QVBoxLayout * vbox = new QVBoxLayout;
    dlg.setLayout( vbox );

    vbox->addWidget( new QLabel( Spell::tr( "Search Index" ) ) );
    auto findVal = new QSpinBox();
    findVal->setRange(0, maxIdx);
    vbox->addWidget( findVal );

    vbox->addWidget( new QLabel( Spell::tr( "Replacement Color" ) ) );

    QGridLayout * grid = new QGridLayout;
    vbox->addLayout( grid );

    ColorWheel * hsv = new ColorWheel;
    grid->addWidget( hsv, 0, 0, 1, 2 );
    hsv->setAlpha( alphaEnable );


    AlphaSlider * alpha = new AlphaSlider;
    alpha->setValue( 1.0f );
    hsv->setAlphaValue( 1.0f );
    alpha->setOrientation( Qt::Vertical );
    grid->addWidget( alpha, 0, 2 );
    alpha->setVisible( alphaEnable );
    QObject::connect( hsv, &ColorWheel::sigColor, alpha, &AlphaSlider::setColor );
    QObject::connect( alpha, &AlphaSlider::valueChanged, hsv, &ColorWheel::setAlphaValue );

    QLabel* value = new QLabel(QString::number(alpha->value(), 'f', 3 ));
    grid->addWidget( value, 1, 2 );
    QObject::connect(alpha, &AlphaSlider::valueChanged,
                     [=](float v){value->setText(QString::number(v, 'f', 3 ));}
                     );


    QHBoxLayout * hbox = new QHBoxLayout;
    vbox->addLayout( hbox );

    QPushButton * ok = new QPushButton;
    ok->setText( Spell::tr( "Ok" ) );
    hbox->addWidget( ok );

    QPushButton * cancel = new QPushButton;
    cancel->setText( Spell::tr( "Cancel" ) );
    hbox->addWidget( cancel );

    QObject::connect( ok, &QPushButton::clicked, &dlg, &QDialog::accept );
    QObject::connect( cancel, &QPushButton::clicked, &dlg, &QDialog::reject );

    if ( dlg.exec() != QDialog::Accepted ) {
        return false;
    }

    retIdx = findVal->value();
    pickColor = hsv->getColor();
    pickColor.setAlphaF(alpha->value());
    return true;
}
//! Set an array of Colors
class spSetReplaceColor final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "Replace" ); }
    QString page() const override final { return Spell::tr( "Color" ); }
    QIcon icon() const override final { return ColorWheel::getIcon(); }
    bool instant() const override final { return true; }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        return nif->isArray( index ) && nif->getValue( index.child( 0, 0 ) ).isColor();
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        QModelIndex colorIdx = (nif->isArray( index )) ? index.child( 0, 0 ) : index;
        auto typ = nif->getValue( colorIdx ).type();
        if ( typ == NifValue::tColor3 )
        {
            auto colors = nif->getArray<Color3>(index);
            QColor pick;
            int idx;
            if(!getColorDialogue(colors.size()-1, false, idx,pick))
                return index;

            auto searchColor = colors[idx];
            Color3 repl = Color3{pick};
            for(int i = 0; i < colors.size(); ++i)
            {
                if(colors[i] == searchColor)
                {
                    colors[i] = repl;
                }
            }

            nif->setArray<Color3>(index, colors);
        }
        else if ( typ == NifValue::tColor4 )
        {
            auto colors = nif->getArray<Color4>(index);

            QColor pick;
            int idx;
            if(!getColorDialogue(colors.size()-1, true, idx,pick))
                return index;

            auto searchColor = colors[idx];
            Color4 repl = Color4{pick};
            for(int i = 0; i < colors.size(); ++i)
            {
                if(colors[i] == searchColor)
                {
                    colors[i] = repl;
                }
            }

            nif->setArray<Color4>(index, colors);
        }

        return index;
    }
};

REGISTER_SPELL( spSetReplaceColor )
