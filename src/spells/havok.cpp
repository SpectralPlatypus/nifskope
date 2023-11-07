#include "spellbook.h"

#include "spells/blocks.h"

#include "havok.h"

#include "lib/nvtristripwrapper.h"
#include "lib/qhull.h"

#define ENABLE_VHACD_IMPLEMENTATION 1
#include "VHACD.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>

#include <algorithm> // std::sort
#include <optional> // std::optional

// Brief description is deliberately not autolinked to class Spell
/*! \file havok.cpp
 * \brief Havok spells
 *
 * All classes here inherit from the Spell class.
 */


//! For Havok coordinate transforms
static const float havokConst = 7.0;
struct CVSResult
{
public:
    QVector<Vector4> Verts;
    QVector<Vector4> Norms;
    float CollRadius;


    CVSResult(QVector<Vector4>&& v, QVector<Vector4>&& n, float cr) :
        Verts {std::move(v)},
        Norms {std::move(n)},
        CollRadius {cr}
    {}
};

std::optional<CVSResult> createCVS(const QVector<Vector3>& verts, float havokScale)
{
    /* those will be filled with the CVS data */
    QVector<Vector4> convex_verts, convex_norms;


    // to store results
    QVector<Vector4> hullVerts, hullNorms;

    // ask for precision
    QDialog dlg;
    QVBoxLayout * vbox = new QVBoxLayout;
    dlg.setLayout( vbox );

    vbox->addWidget( new QLabel( Spell::tr( "Enter the maximum roundoff error to use" ) ) );
    vbox->addWidget( new QLabel( Spell::tr( "Larger values will give a less precise but better performing hull" ) ) );

    QDoubleSpinBox * precSpin = new QDoubleSpinBox;
    precSpin->setRange( 0, 5 );
    precSpin->setDecimals( 3 );
    precSpin->setSingleStep( 0.01 );
    precSpin->setValue( 0.25 );
    vbox->addWidget( precSpin );

    vbox->addWidget( new QLabel( Spell::tr( "Collision Radius" ) ) );

    QDoubleSpinBox * spnRadius = new QDoubleSpinBox;
    spnRadius->setRange( 0, 0.5 );
    spnRadius->setDecimals( 4 );
    spnRadius->setSingleStep( 0.001 );
    spnRadius->setValue( 0.05 );
    vbox->addWidget( spnRadius );

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
        return std::nullopt;
    }

    /* make a convex hull from it */
    compute_convex_hull( verts, hullVerts, hullNorms, (float)precSpin->value() );

    // sort and remove duplicate vertices
    QList<Vector4> sortedVerts;
    for ( Vector4 vert : hullVerts ) {
        vert /= havokScale;

        if ( !sortedVerts.contains( vert ) ) {
            sortedVerts.append( vert );
        }
    }
    std::sort( sortedVerts.begin(), sortedVerts.end(), Vector4::lexLessThan );
    QListIterator<Vector4> vertIter( sortedVerts );

    while ( vertIter.hasNext() ) {
        Vector4 sorted = vertIter.next();
        convex_verts.append( sorted );
    }

    // sort and remove duplicate normals
    QList<Vector4> sortedNorms;
    for ( Vector4 norm : hullNorms ) {
        norm = Vector4( Vector3( norm ), norm[3] / havokScale );

        if ( !sortedNorms.contains( norm ) ) {
            sortedNorms.append( norm );
        }
    }
    std::sort( sortedNorms.begin(), sortedNorms.end(), Vector4::lexLessThan );
    QListIterator<Vector4> normIter( sortedNorms );

    while ( normIter.hasNext() ) {
        Vector4 sorted = normIter.next();
        convex_norms.append( sorted );
    }

    CVSResult cc{std::move(convex_verts), std::move(convex_norms), float(spnRadius->value())};
    return std::optional<CVSResult>(cc);
}

//! Creates a convex hull using Qhull
class spCreateCVS final : public Spell
{

public:
    QString name() const override final { return Spell::tr( "Create Convex Shape" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        if ( !nif->inherits( index, "NiTriBasedGeom" ) || !nif->checkVersion( 0x0A000100, 0 ) )
            return false;

        QModelIndex iData = nif->getBlock( nif->getLink( index, "Data" ) );
        return iData.isValid();
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        QModelIndex iData = nif->getBlock( nif->getLink( index, "Data" ) );

        if ( !iData.isValid() )
            return index;

        float havokScale = (nif->checkVersion( 0x14020007, 0x14020007 ) && nif->getUserVersion() >= 12) ? 10.0f : 1.0f;

        havokScale *= havokConst;

        /* those will be filled with the CVS data */
        QVector<Vector4> convex_verts, convex_norms;

        /* get the verts of our mesh */
        QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
        QVector<Vector3> vertsTrans;

        // Offset by translation of NiTriShape
        Vector3 trans = nif->get<Vector3>( index, "Translation" );
        for ( auto v : verts ) {
            vertsTrans.append( v + trans );
        }

        auto cvsResult = createCVS(vertsTrans, havokScale);
        if(!cvsResult.has_value())
            return index;

        /* create the CVS block */
        QModelIndex iCVS = nif->insertNiBlock( "bhkConvexVerticesShape" );

        /* set CVS verts */
        nif->set<uint>( iCVS, "Num Vertices", cvsResult->Verts.count() );
        nif->updateArray( iCVS, "Vertices" );
        nif->setArray<Vector4>( iCVS, "Vertices", cvsResult->Verts );

        /* set CVS norms */
        nif->set<uint>( iCVS, "Num Normals", cvsResult->Norms.count() );
        nif->updateArray( iCVS, "Normals" );
        nif->setArray<Vector4>( iCVS, "Normals", cvsResult->Norms );

        // radius is always 0.1?
        // TODO: Figure out if radius is not arbitrarily set in vanilla NIFs
        nif->set<float>( iCVS, "Radius", cvsResult->CollRadius );

        // for arrow detection: [0, 0, -0, 0, 0, -0]
        nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 2, 0 ), -0.0 );
        nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 5, 0 ), -0.0 );

        QModelIndex iParent = nif->getBlock( nif->getParent( nif->getBlockNumber( index ) ) );
        QModelIndex collisionLink = nif->getIndex( iParent, "Collision Object" );
        QModelIndex collisionObject = nif->getBlock( nif->getLink( collisionLink ) );

        // create bhkCollisionObject
        if ( !collisionObject.isValid() ) {
            collisionObject = nif->insertNiBlock( "bhkCollisionObject" );

            nif->setLink( collisionLink, nif->getBlockNumber( collisionObject ) );
            nif->setLink( collisionObject, "Target", nif->getBlockNumber( iParent ) );
        }

        QModelIndex rigidBodyLink = nif->getIndex( collisionObject, "Body" );
        QModelIndex rigidBody = nif->getBlock( nif->getLink( rigidBodyLink ) );

        // create bhkRigidBody
        if ( !rigidBody.isValid() ) {
            rigidBody = nif->insertNiBlock( "bhkRigidBody" );

            nif->setLink( rigidBodyLink, nif->getBlockNumber( rigidBody ) );
        }

        QModelIndex shapeLink = nif->getIndex( rigidBody, "Shape" );
        QModelIndex shape = nif->getBlock( nif->getLink( shapeLink ) );

        // set link and delete old one
        nif->setLink( shapeLink, nif->getBlockNumber( iCVS ) );

        if ( shape.isValid() ) {
            // cheaper than calling spRemoveBranch
            nif->removeNiBlock( nif->getBlockNumber( shape ) );
        }

        Message::info( nullptr, Spell::tr( "Created hull with %1 vertices, %2 normals" ).arg( cvsResult->Verts.count() ).arg( cvsResult->Verts.count() ) );

        // returning iCVS here can crash NifSkope if a child array is selected
        return index;
    }
};

REGISTER_SPELL( spCreateCVS )

class spCreateCombinedCVS final : public Spell
{

public:
    QString name() const override final { return Spell::tr( "Create Combined Convex Shape" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        return nif && nif->getRootLinks().count() == 1 &&!index.isValid() && nif->checkVersion( 0x0A000100, 0 );
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        float havokScale = (nif->checkVersion( 0x14020007, 0x14020007 ) && nif->getUserVersion() >= 12) ? 10.0f : 1.0f;

        havokScale *= havokConst;

         /* those will be filled with the CVS data */
         QList<qint32> lTris;

         for ( int n = 0; n < nif->getBlockCount(); n++ ) {
            QModelIndex iBlock = nif->getBlock( n );

            QModelIndex iNumChildren = nif->getIndex( iBlock, "Num Children" );
            QModelIndex iChildren = nif->getIndex( iBlock, "Children" );

            if ( iNumChildren.isValid() && iChildren.isValid() ) {

                for ( int r = 0; r < nif->rowCount( iChildren ); r++ )
                {
                    qint32 lChild = nif->getLink( iChildren.child( r, 0 ) );
                    QModelIndex iChild = nif->getBlock( lChild );

                    if ( nif->isNiBlock( iChild, { "NiTriShape", "NiTriStrips" } ) ){
                        lTris << lChild;
                    }
                }
            }
        }

        QVector<Vector3> vertsTrans;
        for ( const auto lTri : lTris)
        {
            combine(nif, nif->getBlock( lTri ), vertsTrans);
        }

        auto cvsResult = createCVS(vertsTrans, havokScale);
        if(!cvsResult.has_value())
            return index;

       const auto& convex_verts = cvsResult->Verts;
       const auto& convex_norms = cvsResult->Norms;

        /* create the CVS block */
        QModelIndex iCVS = nif->insertNiBlock( "bhkConvexVerticesShape" );

        /* set CVS verts */
        nif->set<uint>( iCVS, "Num Vertices", convex_verts.count() );
        nif->updateArray( iCVS, "Vertices" );
        nif->setArray<Vector4>( iCVS, "Vertices", convex_verts );

        /* set CVS norms */
        nif->set<uint>( iCVS, "Num Normals", convex_norms.count() );
        nif->updateArray( iCVS, "Normals" );
        nif->setArray<Vector4>( iCVS, "Normals", convex_norms );

        // radius is always 0.1?
        // TODO: Figure out if radius is not arbitrarily set in vanilla NIFs
        nif->set<float>( iCVS, "Radius", cvsResult->CollRadius );

        // for arrow detection: [0, 0, -0, 0, 0, -0]
        nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 2, 0 ), -0.0 );
        nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 5, 0 ), -0.0 );

        QModelIndex iParent = nif->getBlock( nif->getRootLinks()[0] );
        QModelIndex collisionLink = nif->getIndex( iParent, "Collision Object" );
        QModelIndex collisionObject = nif->getBlock( nif->getLink( collisionLink ) );

        // create bhkCollisionObject
        if ( !collisionObject.isValid() ) {
            collisionObject = nif->insertNiBlock( "bhkCollisionObject" );

            nif->setLink( collisionLink, nif->getBlockNumber( collisionObject ) );
            nif->setLink( collisionObject, "Target", nif->getBlockNumber( iParent ) );
        }

        QModelIndex rigidBodyLink = nif->getIndex( collisionObject, "Body" );
        QModelIndex rigidBody = nif->getBlock( nif->getLink( rigidBodyLink ) );

        // create bhkRigidBody
        if ( !rigidBody.isValid() ) {
            rigidBody = nif->insertNiBlock( "bhkRigidBody" );

            nif->setLink( rigidBodyLink, nif->getBlockNumber( rigidBody ) );
        }

        QModelIndex shapeLink = nif->getIndex( rigidBody, "Shape" );
        QModelIndex shape = nif->getBlock( nif->getLink( shapeLink ) );

        // set link and delete old one
        nif->setLink( shapeLink, nif->getBlockNumber( iCVS ) );

        if ( shape.isValid() ) {
            // cheaper than calling spRemoveBranch
            nif->removeNiBlock( nif->getBlockNumber( shape ) );
        }

        Message::info( nullptr, Spell::tr( "Created hull with %1 vertices, %2 normals" ).arg( convex_verts.count() ).arg( convex_norms.count() ) );

        // returning iCVS here can crash NifSkope if a child array is selected
        return index;
    }

    void combine( NifModel * nif, const QModelIndex& lTri, QVector<Vector3> &points)
    {
        QModelIndex iData = nif->getBlock( nif->getLink( lTri, "Data" ), "NiTriBasedGeomData" );

        if ( !iData.isValid() ) return;

        QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
        Vector3 trans = nif->get<Vector3>( iData, "Translation" );

        for ( auto v : verts ) {
            points.append( v + trans );
        }
    }
};

REGISTER_SPELL( spCreateCombinedCVS )

//! Transforms Havok constraints
class spConstraintHelper final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "A -> B" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        return nif &&
                nif->isNiBlock( nif->getBlock( index ),
                                { "bhkMalleableConstraint",
                                  "bhkBreakableConstraint",
                                  "bhkRagdollConstraint",
                                  "bhkLimitedHingeConstraint",
                                  "bhkHingeConstraint",
                                  "bhkPrismaticConstraint" }
                                );
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        QModelIndex iConstraint = nif->getBlock( index );
        QString name = nif->itemName( iConstraint );

        if ( name == "bhkMalleableConstraint" || name == "bhkBreakableConstraint" ) {
            if ( nif->getIndex( iConstraint, "Ragdoll" ).isValid() ) {
                name = "bhkRagdollConstraint";
            } else if ( nif->getIndex( iConstraint, "Limited Hinge" ).isValid() ) {
                name = "bhkLimitedHingeConstraint";
            } else if ( nif->getIndex( iConstraint, "Hinge" ).isValid() ) {
                name = "bhkHingeConstraint";
            }
        }

        QModelIndex iBodyA = nif->getBlock( nif->getLink( nif->getIndex( iConstraint, "Entities" ).child( 0, 0 ) ), "bhkRigidBody" );
        QModelIndex iBodyB = nif->getBlock( nif->getLink( nif->getIndex( iConstraint, "Entities" ).child( 1, 0 ) ), "bhkRigidBody" );

        if ( !iBodyA.isValid() || !iBodyB.isValid() ) {
            Message::warning( nullptr, Spell::tr( "Couldn't find the bodies for this constraint." ) );
            return index;
        }

        Transform transA = bodyTrans( nif, iBodyA );
        Transform transB = bodyTrans( nif, iBodyB );

        QModelIndex iConstraintData;
        if ( name == "bhkLimitedHingeConstraint" ) {
            iConstraintData = nif->getIndex( iConstraint, "Limited Hinge" );
            if ( !iConstraintData.isValid() )
                iConstraintData = iConstraint;
        } else if ( name == "bhkRagdollConstraint" ) {
            iConstraintData = nif->getIndex( iConstraint, "Ragdoll" );
            if ( !iConstraintData.isValid() )
                iConstraintData = iConstraint;
        } else if ( name == "bhkHingeConstraint" ) {
            iConstraintData = nif->getIndex( iConstraint, "Hinge" );
            if ( !iConstraintData.isValid() )
                iConstraintData = iConstraint;
        }

        if ( !iConstraintData.isValid() )
            return index;

        Vector3 pivot = Vector3( nif->get<Vector4>( iConstraintData, "Pivot A" ) ) * havokConst;
        pivot = transA * pivot;
        pivot = transB.rotation.inverted() * ( pivot - transB.translation ) / transB.scale / havokConst;
        nif->set<Vector4>( iConstraintData, "Pivot B", { pivot[0], pivot[1], pivot[2], 0 } );

        QString axleA, axleB, twistA, twistB, twistA2, twistB2;
        if ( name.endsWith( "HingeConstraint" ) ) {
            axleA = "Axle A";
            axleB = "Axle B";
            twistA = "Perp2 Axle In A1";
            twistB = "Perp2 Axle In B1";
            twistA2 = "Perp2 Axle In A2";
            twistB2 = "Perp2 Axle In B2";
        } else if ( name == "bhkRagdollConstraint" ) {
            axleA = "Plane A";
            axleB = "Plane B";
            twistA = "Twist A";
            twistB = "Twist B";
        }

        if ( axleA.isEmpty() || axleB.isEmpty() || twistA.isEmpty() || twistB.isEmpty() )
            return index;

        Vector3 axle = Vector3( nif->get<Vector4>( iConstraintData, axleA ) );
        axle = transA.rotation * axle;
        axle = transB.rotation.inverted() * axle;
        nif->set<Vector4>( iConstraintData, axleB, { axle[0], axle[1], axle[2], 0 } );

        axle = Vector3( nif->get<Vector4>( iConstraintData, twistA ) );
        axle = transA.rotation * axle;
        axle = transB.rotation.inverted() * axle;
        nif->set<Vector4>( iConstraintData, twistB, { axle[0], axle[1], axle[2], 0 } );

        if ( !twistA2.isEmpty() && !twistB2.isEmpty() ) {
            axle = Vector3( nif->get<Vector4>( iConstraintData, twistA2 ) );
            axle = transA.rotation * axle;
            axle = transB.rotation.inverted() * axle;
            nif->set<Vector4>( iConstraintData, twistB2, { axle[0], axle[1], axle[2], 0 } );
        }

        return index;
    }

    static Transform bodyTrans( const NifModel * nif, const QModelIndex & index )
    {
        Transform t;

        if ( nif->isNiBlock( index, "bhkRigidBodyT" ) ) {
            t.translation = Vector3( nif->get<Vector4>( index, "Translation" ) * 7 );
            t.rotation.fromQuat( nif->get<Quat>( index, "Rotation" ) );
        }

        qint32 l = nif->getBlockNumber( index );

        while ( ( l = nif->getParent( l ) ) >= 0 ) {
            QModelIndex iAV = nif->getBlock( l, "NiAVObject" );

            if ( iAV.isValid() )
                t = Transform( nif, iAV ) * t;
        }

        return t;
    }
};

REGISTER_SPELL( spConstraintHelper )

//! Calculates Havok spring lengths
class spStiffSpringHelper final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "Calculate Spring Length" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
    {
        return nif && nif->isNiBlock( nif->getBlock( idx ), "bhkStiffSpringConstraint" );
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & idx ) override final
    {
        QModelIndex iConstraint = nif->getBlock( idx );
        QModelIndex iSpring = nif->getIndex( iConstraint, "Stiff Spring" );
        if ( !iSpring.isValid() )
            iSpring = iConstraint;

        QModelIndex iBodyA = nif->getBlock( nif->getLink( nif->getIndex( iConstraint, "Entities" ).child( 0, 0 ) ), "bhkRigidBody" );
        QModelIndex iBodyB = nif->getBlock( nif->getLink( nif->getIndex( iConstraint, "Entities" ).child( 1, 0 ) ), "bhkRigidBody" );

        if ( !iBodyA.isValid() || !iBodyB.isValid() ) {
            Message::warning( nullptr, Spell::tr( "Couldn't find the bodies for this constraint" ) );
            return idx;
        }

        Transform transA = spConstraintHelper::bodyTrans( nif, iBodyA );
        Transform transB = spConstraintHelper::bodyTrans( nif, iBodyB );

        Vector3 pivotA( nif->get<Vector4>( iSpring, "Pivot A" ) * 7 );
        Vector3 pivotB( nif->get<Vector4>( iSpring, "Pivot B" ) * 7 );

        float length = ( transA * pivotA - transB * pivotB ).length() / 7;

        nif->set<float>( iSpring, "Length", length );

        return nif->getIndex( iSpring, "Length" );
    }
};

REGISTER_SPELL( spStiffSpringHelper )

//! Packs Havok strips
class spPackHavokStrips final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "Pack Strips" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
    {
        return nif->isNiBlock( idx, "bhkNiTriStripsShape" );
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & iBlock ) override final
    {
        QPersistentModelIndex iShape( iBlock );

        QVector<Vector3> vertices;
        QVector<Triangle> triangles;
        QVector<Vector3> normals;

        for ( const auto lData : nif->getLinkArray( iShape, "Strips Data" ) ) {
            QModelIndex iData = nif->getBlock( lData, "NiTriStripsData" );

            if ( iData.isValid() ) {
                QVector<Vector3> vrts = nif->getArray<Vector3>( iData, "Vertices" );
                QVector<Triangle> tris;
                QVector<Vector3> nrms;

                QModelIndex iPoints = nif->getIndex( iData, "Points" );

                for ( int x = 0; x < nif->rowCount( iPoints ); x++ ) {
                    tris += triangulate( nif->getArray<quint16>( iPoints.child( x, 0 ) ) );
                }

                QMutableVectorIterator<Triangle> it( tris );

                while ( it.hasNext() ) {
                    Triangle & tri = it.next();

                    Vector3 a = vrts.value( tri[0] );
                    Vector3 b = vrts.value( tri[1] );
                    Vector3 c = vrts.value( tri[2] );

                    nrms << Vector3::crossproduct( b - a, c - a ).normalize();

                    tri[0] += vertices.count();
                    tri[1] += vertices.count();
                    tri[2] += vertices.count();
                }

                for ( const Vector3& v : vrts ) {
                    vertices += v / 7;
                }
                triangles += tris;
                normals += nrms;
            }
        }

        if ( vertices.isEmpty() || triangles.isEmpty() ) {
            Message::warning( nullptr, Spell::tr( "No mesh data was found." ) );
            return iShape;
        }

        QPersistentModelIndex iPackedShape = nif->insertNiBlock( "bhkPackedNiTriStripsShape", nif->getBlockNumber( iShape ) );

        nif->set<int>( iPackedShape, "Num Sub Shapes", 1 );
        QModelIndex iSubShapes = nif->getIndex( iPackedShape, "Sub Shapes" );
        nif->updateArray( iSubShapes );
        nif->set<int>( iSubShapes.child( 0, 0 ), "Layer", 1 );
        nif->set<int>( iSubShapes.child( 0, 0 ), "Num Vertices", vertices.count() );
        nif->set<int>( iSubShapes.child( 0, 0 ), "Material", nif->get<int>( iShape, "Material" ) );
        nif->setArray<float>( iPackedShape, "Unknown Floats", { 0.0f, 0.0f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.1f } );
        nif->set<float>( iPackedShape, "Scale", 1.0f );
        nif->setArray<float>( iPackedShape, "Unknown Floats 2", { 1.0f, 1.0f, 1.0f } );

        QModelIndex iPackedData = nif->insertNiBlock( "hkPackedNiTriStripsData", nif->getBlockNumber( iPackedShape ) );
        nif->setLink( iPackedShape, "Data", nif->getBlockNumber( iPackedData ) );

        nif->set<int>( iPackedData, "Num Triangles", triangles.count() );
        QModelIndex iTriangles = nif->getIndex( iPackedData, "Triangles" );
        nif->updateArray( iTriangles );

        for ( int t = 0; t < triangles.size(); t++ ) {
            nif->set<Triangle>( iTriangles.child( t, 0 ), "Triangle", triangles[ t ] );
            nif->set<Vector3>( iTriangles.child( t, 0 ), "Normal", normals.value( t ) );
        }

        nif->set<int>( iPackedData, "Num Vertices", vertices.count() );
        QModelIndex iVertices = nif->getIndex( iPackedData, "Vertices" );
        nif->updateArray( iVertices );
        nif->setArray<Vector3>( iVertices, vertices );

        QMap<qint32, qint32> lnkmap;
        lnkmap.insert( nif->getBlockNumber( iShape ), nif->getBlockNumber( iPackedShape ) );
        nif->mapLinks( lnkmap );

        // *** THIS SOMETIMES CRASHES NIFSKOPE        ***
        // *** UNCOMMENT WHEN BRANCH REMOVER IS FIXED ***
        // See issue #2508255
        spRemoveBranch BranchRemover;
        BranchRemover.castIfApplicable( nif, iShape );

        return iPackedShape;
    }
};

REGISTER_SPELL( spPackHavokStrips )

// Skyrim SE Convex Decomposition with MoppTree
#ifdef Q_OS_WIN32

// This code is only intended to be run with Win32 platform.

extern "C" void * __stdcall SetDllDirectoryA( const char * lpPathName );
extern "C" void * __stdcall LoadLibraryA( const char * lpModuleName );
extern "C" void * __stdcall GetProcAddress ( void * hModule, const char * lpProcName );
extern "C" void __stdcall FreeLibrary( void * lpModule );

//! Interface to the external MOPP library
class HavokMoppCode
{
private:

    typedef bool (__stdcall * fnGetMoppCode)(std::vector<std::vector<Vector4>> vertexIn, quint8** moppCodeOut, size_t* nCodeOut, Vector3* origin, float* scale);
    using fnAddVertices = void(*)(float* vertexIn, std::size_t vertexCount, int strideLen);
    using fnComputeMoppCode = bool(*)(Vector3 * origin, float* scale, std::size_t * nCodeOut);
    using fnFinalize = void(*)(std::uint8_t* moppCode);

    void * hMoppGenLib;
    fnGetMoppCode GetMoppCode;
    fnAddVertices AddVertices;
    fnComputeMoppCode ComputeMoppCode;
    fnFinalize Finalize;
public:
    HavokMoppCode() :
        hMoppGenLib( 0 ),
        GetMoppCode(0),
        AddVertices(nullptr),
        ComputeMoppCode(nullptr),
        Finalize(nullptr)
    {
    }

    ~HavokMoppCode()
    {
        if ( hMoppGenLib )
            FreeLibrary( hMoppGenLib );
    }

    bool Initialize()
    {
        if ( !hMoppGenLib ) {
            SetDllDirectoryA( QCoreApplication::applicationDirPath().toLocal8Bit().constData() );
            hMoppGenLib = LoadLibraryA( "MoppGen.dll" );
            GetMoppCode = (fnGetMoppCode)GetProcAddress(hMoppGenLib, "GetMoppCode");
            AddVertices = (fnAddVertices)(GetProcAddress(hMoppGenLib,"AddVertices"));
            ComputeMoppCode = (fnComputeMoppCode)(GetProcAddress(hMoppGenLib,"ComputeMoppCode"));
            Finalize = (fnFinalize)(GetProcAddress(hMoppGenLib,"Finalize"));
        }

        return AddVertices && ComputeMoppCode && Finalize;
    }

    void AddVertex(QVector<Vector4> vertexIn)
    {
        AddVertices((float*)vertexIn.data(), vertexIn.count(), 4);
    }
    QByteArray RetrieveMoppCode(Vector3* originOut, float* scaleOut)
    {
        QByteArray code;
        size_t moppLen;

        ComputeMoppCode(originOut, scaleOut, &moppLen);

        if(moppLen > 0)
        {
            code.resize(moppLen);
            Finalize((quint8*)code.data());
            return code;
        }

        return nullptr;
    }
    QByteArray GetConvexMoppCode(std::vector<std::vector<Vector4>> input, Vector3* originOut, float* scaleOut)
    {
        unsigned char* buf;
        size_t bufLen;


        if(GetMoppCode(input, &buf, &bufLen, originOut, scaleOut))
        {
            QByteArray moppCode((char*)buf, bufLen);
            return moppCode;
        }
        return nullptr;
    }
}
TheMoppet;

//! Creates a convex hull using Qhull
class spCreateHACD final : public Spell
{

private:
    VHacdDialog::DialValues dialValues;
public:
    QString name() const override final { return Spell::tr( "Create Convex Decomposition" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    spCreateHACD() :
        dialValues (400000, 16, 0.01, 16, false, 4)
    {
    }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        if ( TheMoppet.Initialize() )
        {
            if ( !nif->inherits( index, "NiTriBasedGeom" ) || !nif->checkVersion( 0x0A000100, 0 ) )
                return false;

            QModelIndex iData = nif->getBlock( nif->getLink( index, "Data" ) );
            return iData.isValid();
        }

        return false;
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        // moppTree
        Vector3 origin;
        float scale;
        QByteArray moppCode;

        QModelIndex iData = nif->getBlock( nif->getLink( index, "Data" ) );

        if ( !iData.isValid() )
            return index;

        float havokScale = (nif->checkVersion( 0x14020007, 0x14020007 ) && nif->getUserVersion() >= 12) ? 10.0f : 1.0f;

        havokScale *= havokConst;

        /* those will be filled with the CVS data */
        VHacdDialog dlg{};
        dlg.setParams(dialValues);

         if ( dlg.exec() != QDialog::Accepted ) {
             // Save values for next call
             return index;
         }

         dialValues = dlg.getParams();

        /* get the verts of our mesh */
        QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
        QVector<Vector3> vertsTrans;
        QVector<float> points;

        QVector<Triangle> tris = nif->getArray<Triangle>(iData, "Triangles");
        QVector<uint32_t> triangles;
        // Offset by translation of NiTriShape
        Vector3 trans = nif->get<Vector3>( index, "Translation" );
        //Scale
        float triShapeScale = nif->get<float>(index, "Scale");
        for ( auto v : verts ) {
            vertsTrans.append( v + trans );
            points.append(v[0] * triShapeScale + trans[0]);
            points.append(v[1] * triShapeScale + trans[1]);
            points.append(v[2] * triShapeScale + trans[2]);
        }

        // Add triangles
        for(auto t : tris)
        {
            triangles.append(t[0]);
            triangles.append(t[1]);
            triangles.append(t[2]);
        }

        VHACD::IVHACD* interfaceVHACD = VHACD::CreateVHACD();
        VHACD::IVHACD::Parameters params {
            .m_maxConvexHulls = dialValues.maxConvexHulls,
            .m_resolution = dialValues.resolution,
            .m_minimumVolumePercentErrorAllowed = dialValues.minimumVolumePercentErrorAllowed,
            .m_maxNumVerticesPerCH = dialValues.maxNumVerticesPerCH,
        };

        switch(dialValues.fillMethod){
            case VHacdDialog::DialValues::FillMethod::FloodFill: params.m_fillMode = VHACD::FillMode::FLOOD_FILL; break;
            case VHacdDialog::DialValues::FillMethod::Surface: params.m_fillMode = VHACD::FillMode::SURFACE_ONLY; break;
            case VHacdDialog::DialValues::FillMethod::Raycast: params.m_fillMode = VHACD::FillMode::RAYCAST_FILL; break;
       }

        bool res = interfaceVHACD->Compute(&points[0], (unsigned int)points.size() / 3,
                (const uint32_t *)&triangles[0], (unsigned int)triangles.size() / 3, params);

        if (!res)
        {
            return index;
        }

        QVector<qint32> shapeList;
        QModelIndex iBLS = nif->insertNiBlock("bhkListShape");

        bool ok;
        quint32 enumVal;
        auto matlsStrings = NifValue::enumOptions("SkyrimHavokMaterial");
        enumVal = NifValue::enumOptionValue("SkyrimHavokMaterial", matlsStrings.at(dialValues.matlsIndex), &ok);
        if(!ok) enumVal = 0;

        for (unsigned int p = 0; p < interfaceVHACD->GetNConvexHulls(); ++p)
        {
            QVector<Vector4> hullVerts;
            QVector<Vector4> hullNorms;
            VHACD::IVHACD::ConvexHull ch;
            interfaceVHACD->GetConvexHull(p, ch);
            // Get points
            for (const auto& vec : ch.m_points) {
                Vector4 v;
                v[0] = vec[0];
                v[1] = vec[1];
                v[2] = vec[2];
                v /= havokScale;
                hullVerts.append(v);
            }

            TheMoppet.AddVertex(hullVerts);
            for (const auto& tri : ch.m_triangles) {
                Triangle t {
                    (quint16)tri.mI0,
                            (quint16)tri.mI1,
                            (quint16)tri.mI2
                };


                Vector3 u {hullVerts[t[1]] - hullVerts[t[0]]};
                Vector3 v {hullVerts[t[2]] - hullVerts[t[0]]};

                Vector3 n = Vector3::crossproduct(u,v);
                n = n.normalize();

                hullNorms.append(Vector4{n});
            }

            QModelIndex iCVS = nif->insertNiBlock( "bhkConvexVerticesShape" );

            /* set CVS verts */
            nif->set<uint>( iCVS, "Num Vertices", hullVerts.count() );
            nif->updateArray( iCVS, "Vertices" );
            nif->setArray<Vector4>( iCVS, "Vertices", hullVerts);
            nif->set<float>( iCVS, "Radius", 0.001f);
            /* set CVS norms */
            nif->set<uint>( iCVS, "Num Normals", hullNorms.count() );
            nif->updateArray( iCVS, "Normals" );
            nif->setArray<Vector4>( iCVS, "Normals", hullNorms);

            // for arrow detection: [0, 0, -0, 0, 0, -0]
            nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 2, 0 ), -0.0 );
            nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 5, 0 ), -0.0 );

            nif->set<quint32>(iCVS, "Material", enumVal);

            shapeList.append(nif->getBlockNumber(iCVS));
        }


        // Add all entries to shapeList
        QModelIndex shapeArray = nif->getIndex(iBLS, "Sub Shapes");
        nif->set<uint>(iBLS, "Num Sub Shapes", shapeList.count());
        nif->updateArray(shapeArray);
        nif->setLinkArray(shapeArray, shapeList);
        nif->set<quint32>(iBLS, "Material", enumVal);

        // TODO: How does one achieve multiple collision items?
        QModelIndex iParent = nif->getBlock( nif->getParent( nif->getBlock( index ) ) );
        QModelIndex collisionLink = nif->getIndex( iParent, "Collision Object" );
        QModelIndex collisionObject = nif->getBlock( nif->getLink( collisionLink ) );

        // create bhkCollisionObject
        if ( !collisionObject.isValid() ) {
            collisionObject = nif->insertNiBlock( "bhkCollisionObject" );

            nif->setLink( collisionLink, nif->getBlockNumber( collisionObject ) );
            nif->setLink( collisionObject, "Target", nif->getBlockNumber( iParent ) );
        }

        // create moppBvTreeShape (optionally)
        if(shapeList.count() >  4)
        {
            moppCode = TheMoppet.RetrieveMoppCode(&origin, &scale);
            if ( moppCode.size() == 0 ) {
                Message::critical( nullptr, Spell::tr( "Failed to generate MOPP code" ) );
            } else {
                QModelIndex ibhkMoppBvTreeShape = nif->insertNiBlock( "bhkMoppBvTreeShape" );
                QModelIndex shapeLink = nif->getIndex( ibhkMoppBvTreeShape, "Shape" );
                QModelIndex shape = nif->getBlock( nif->getLink( shapeLink ) );
                nif->setLink( shapeLink, nif->getBlockNumber( iBLS ) );
                iBLS = ibhkMoppBvTreeShape;

                if ( shape.isValid() ) {
                    // TODO: Remove children (this is turning into a monstrosity)
                    nif->removeNiBlock( nif->getBlockNumber( shape ) );
                }
            }
        }

        QModelIndex rigidBodyLink = nif->getIndex( collisionObject, "Body" );
        QModelIndex rigidBody = nif->getBlock( nif->getLink( rigidBodyLink ) );
        // create bhkRigidBody
        if ( !rigidBody.isValid() ) {
            rigidBody = nif->insertNiBlock( "bhkRigidBody" );

            nif->setLink( rigidBodyLink, nif->getBlockNumber( rigidBody ) );
        }

        enumVal = NifValue::enumOptionValue("SkyrimLayer", dialValues.staticCollision ? "SKYL_STATIC" : "SKYL_CLUTTER", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Layer", enumVal);

        //nif->set<quint8>(rigidBody, "Flags and Part Number", 128);
        nif->set<ushort>(rigidBody, "Process Contact Callback Delay", 65535);

        enumVal = NifValue::enumOptionValue("hkMotionType", dialValues.staticCollision ? "MO_SYS_FIXED": "MO_SYS_SPHERE_STABILIZED", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Motion System", enumVal);

        enumVal = NifValue::enumOptionValue("hkQualityType", dialValues.staticCollision ? "MO_QUAL_INVALID" : "MO_QUAL_MOVING", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Quality Type", enumVal); //MO_QUAL_MOVING

        enumVal = NifValue::enumOptionValue("hkSolverDeactivation",dialValues.staticCollision ? "SOLVER_DEACTIVATION_OFF" : "SOLVER_DEACTIVATION_LOW", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Solver Deactivation", enumVal); //SOLVER_DEACTIVATION_LOW

        QModelIndex shapeLink = nif->getIndex( rigidBody, "Shape" );
        QModelIndex shape = nif->getBlock( nif->getLink( shapeLink ) );

        nif->setLink( shapeLink, nif->getBlockNumber( iBLS ) );

        if ( shape.isValid() ) {
            // cheaper than calling spRemoveBranch
            nif->removeNiBlock( nif->getBlockNumber( shape ) );
        }


        QModelIndex moppTreeLink = nif->getIndex( rigidBody, "Shape" );
        QModelIndex ibhkMoppBvTreeShape = nif->getBlock( nif->getLink( moppTreeLink ) );
        if(ibhkMoppBvTreeShape.isValid())
        {
            enumVal = NifValue::enumOptionValue("MoppDataBuildType", "BUILT_WITHOUT_CHUNK_SUBDIVISION", &ok);
            if(ok)
                nif->set<quint8>(ibhkMoppBvTreeShape, "Build Type", enumVal);

            QModelIndex iCodeOrigin = nif->getIndex( ibhkMoppBvTreeShape, "Origin" );
            nif->set<Vector3>( iCodeOrigin, origin );

            QModelIndex iCodeScale = nif->getIndex( ibhkMoppBvTreeShape, "Scale" );
            nif->set<float>( iCodeScale, scale );

            QModelIndex iCodeSize = nif->getIndex( ibhkMoppBvTreeShape, "MOPP Data Size" );
            QModelIndex iCode = nif->getIndex( ibhkMoppBvTreeShape, "MOPP Data" );

            if ( iCodeSize.isValid()) {
                nif->set<int>( iCodeSize, moppCode.size() );
                nif->updateArray( iCode );
                QModelIndex iChild = iCode.child(0,0);
                if(iChild.isValid())
                    nif->set<QByteArray>( iChild, moppCode );
            }
        }

        Message::info( nullptr, Spell::tr( "Created v-hacd with %1 convex surfaces" ).arg( shapeList.count() ));

        // returning iCVS here can crash NifSkope if a child array is selected
        return index;
    }
};

REGISTER_SPELL( spCreateHACD )

class spCreateCombinedHACD final : public Spell
{
private:
    VHacdDialog::DialValues dialValues;
public:
    QString name() const override final { return Spell::tr( "Create Combined Convex Decomposition" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

    spCreateCombinedHACD() :
        dialValues (400000, 16, 0.01, 16, false, 4)
    {
    }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
    {
        if ( TheMoppet.Initialize() )
        {
            //return nif && nif->isNiBlock( index, "NiNode" );
            return nif && nif->getRootLinks().count() == 1 &&!index.isValid();
        }

        return false;
    }

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
    {
        // moppTree
        Vector3 origin;
        float scale;
        QByteArray moppCode;

        float havokScale = (nif->checkVersion( 0x14020007, 0x14020007 ) && nif->getUserVersion() >= 12) ? 10.0f : 1.0f;

        havokScale *= havokConst;

        /* those will be filled with the CVS data */
        //QVector<Vector4> convex_verts, convex_norms;
       VHacdDialog dlg{};
       dlg.setParams(dialValues);

        if ( dlg.exec() != QDialog::Accepted ) {
            // Save values for next call
            return index;
        }

        dialValues = dlg.getParams();

        QList<qint32> lTris;
        for ( int n = 0; n < nif->getBlockCount(); n++ ) {
            QModelIndex iBlock = nif->getBlock( n );

            QModelIndex iNumChildren = nif->getIndex( iBlock, "Num Children" );
            QModelIndex iChildren = nif->getIndex( iBlock, "Children" );

            if ( iNumChildren.isValid() && iChildren.isValid() ) {

                for ( int r = 0; r < nif->rowCount( iChildren ); r++ )
                {
                    qint32 lChild = nif->getLink( iChildren.child( r, 0 ) );
                    QModelIndex iChild = nif->getBlock( lChild );

                    if ( nif->isNiBlock( iChild, { "NiTriShape", "NiTriStrips" } ) ){
                        lTris << lChild;
                    }
                }
            }
        }

        int rootIdx = nif->getRootLinks()[0];
        QPersistentModelIndex iParent = nif->getBlock(rootIdx);

        /* get the verts of our mesh */
        QVector<float> points;
        QVector<uint32_t> triangles;

        for ( const auto lTri : lTris)
        {
            QVector<Vector3> vertsTrans;
            combine(nif, nif->getBlock( lTri ), points, triangles);
        }
//------------------------------------------------------

        VHACD::IVHACD* interfaceVHACD = VHACD::CreateVHACD();
        VHACD::IVHACD::Parameters params {
        .m_maxConvexHulls = dialValues.maxConvexHulls,
        .m_resolution = dialValues.resolution,
        .m_minimumVolumePercentErrorAllowed = dialValues.minimumVolumePercentErrorAllowed,
        .m_maxNumVerticesPerCH = dialValues.maxNumVerticesPerCH,
        };

        bool res = interfaceVHACD->Compute(&points[0], (unsigned int)points.size() / 3,
                (const uint32_t *)&triangles[0], (unsigned int)triangles.size() / 3, params);

        if (!res)
        {
            return index;
        }

        QVector<qint32> shapeList;
        QModelIndex iBLS = nif->insertNiBlock("bhkListShape");

        bool ok;
        quint32 enumVal;

        auto matlsStrings = NifValue::enumOptions("SkyrimHavokMaterial");
        enumVal = NifValue::enumOptionValue("SkyrimHavokMaterial", matlsStrings.at(dialValues.matlsIndex), &ok);
        if(!ok) enumVal = 0;


        for (unsigned int p = 0; p < interfaceVHACD->GetNConvexHulls(); ++p)
        {
            QVector<Vector4> hullVerts;
            QVector<Vector4> hullNorms;
            VHACD::IVHACD::ConvexHull ch;
            interfaceVHACD->GetConvexHull(p, ch);

            for (const auto& vec : ch.m_points) {
                Vector4 v;
                v[0] = vec[0];
                v[1] = vec[1];
                v[2] = vec[2];
                v /= havokScale;
                hullVerts.append(v);
            }

            TheMoppet.AddVertex(hullVerts);
            for (const auto& tri : ch.m_triangles) {
                Triangle t {
                    (quint16)tri.mI0,
                    (quint16)tri.mI1,
                    (quint16)tri.mI2
                };

                Vector3 u {hullVerts[t[1]] - hullVerts[t[0]]};
                Vector3 v {hullVerts[t[2]] - hullVerts[t[0]]};

                Vector3 n = Vector3::crossproduct(u,v);
                n = n.normalize();

                hullNorms.append(Vector4{n});
            }

            QModelIndex iCVS = nif->insertNiBlock( "bhkConvexVerticesShape" );

            /* set CVS verts */
            nif->set<uint>( iCVS, "Num Vertices", hullVerts.count() );
            nif->updateArray( iCVS, "Vertices" );
            nif->setArray<Vector4>( iCVS, "Vertices", hullVerts);
            nif->set<float>( iCVS, "Radius", 0.001f);
            /* set CVS norms */
            nif->set<uint>( iCVS, "Num Normals", hullNorms.count() );
            nif->updateArray( iCVS, "Normals" );
            nif->setArray<Vector4>( iCVS, "Normals", hullNorms);

            // for arrow detection: [0, 0, -0, 0, 0, -0]
            nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 2, 0 ), -0.0 );
            nif->set<float>( nif->getIndex( iCVS, "Unknown 6 Floats" ).child( 5, 0 ), -0.0 );

            nif->set<quint32>(iCVS, "Material", enumVal);

            shapeList.append(nif->getBlockNumber(iCVS));
        }


        // Add all entries to shapeList
        QModelIndex shapeArray = nif->getIndex(iBLS, "Sub Shapes");
        nif->set<uint>(iBLS, "Num Sub Shapes", shapeList.count());
        nif->updateArray(shapeArray);
        nif->setLinkArray(shapeArray, shapeList);
        nif->set<quint32>(iBLS, "Material", enumVal);

        // TODO: How does one achieve multiple collision items?
        QModelIndex collisionLink = nif->getIndex( iParent, "Collision Object" );
        QModelIndex collisionObject = nif->getBlock( nif->getLink( collisionLink ) );

        // create bhkCollisionObject
        if ( !collisionObject.isValid() ) {
            collisionObject = nif->insertNiBlock( "bhkCollisionObject" );

            nif->setLink( collisionLink, nif->getBlockNumber( collisionObject ) );
            nif->setLink( collisionObject, "Target", nif->getBlockNumber( iParent ) );
        }

        // create moppBvTreeShape (optionally)
        if(shapeList.count() >  4)
        {
            moppCode = TheMoppet.RetrieveMoppCode(&origin, &scale);
            if ( moppCode.size() == 0 ) {
                Message::critical( nullptr, Spell::tr( "Failed to generate MOPP code" ) );
            } else {
                QModelIndex ibhkMoppBvTreeShape = nif->insertNiBlock( "bhkMoppBvTreeShape" );
                QModelIndex shapeLink = nif->getIndex( ibhkMoppBvTreeShape, "Shape" );
                QModelIndex shape = nif->getBlock( nif->getLink( shapeLink ) );
                nif->setLink( shapeLink, nif->getBlockNumber( iBLS ) );
                iBLS = ibhkMoppBvTreeShape;

                if ( shape.isValid() ) {
                    // TODO: Remove children (this is turning into a monstrosity)
                    nif->removeNiBlock( nif->getBlockNumber( shape ) );
                }
            }
        }

        QModelIndex rigidBodyLink = nif->getIndex( collisionObject, "Body" );
        QModelIndex rigidBody = nif->getBlock( nif->getLink( rigidBodyLink ) );
        // create bhkRigidBody
        if ( !rigidBody.isValid() ) {
            rigidBody = nif->insertNiBlock( "bhkRigidBody" );

            nif->setLink( rigidBodyLink, nif->getBlockNumber( rigidBody ) );
        }

        enumVal = NifValue::enumOptionValue("SkyrimLayer", dialValues.staticCollision ? "SKYL_STATIC" : "SKYL_CLUTTER", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Layer", enumVal);

        //nif->set<quint8>(rigidBody, "Flags and Part Number", 128);
        nif->set<ushort>(rigidBody, "Process Contact Callback Delay", 65535);

        enumVal = NifValue::enumOptionValue("hkMotionType", dialValues.staticCollision ? "MO_SYS_FIXED": "MO_SYS_SPHERE_STABILIZED", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Motion System", enumVal);

        enumVal = NifValue::enumOptionValue("hkQualityType", dialValues.staticCollision ? "MO_QUAL_INVALID" : "MO_QUAL_MOVING", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Quality Type", enumVal); //MO_QUAL_MOVING

        enumVal = NifValue::enumOptionValue("hkSolverDeactivation",dialValues.staticCollision ? "SOLVER_DEACTIVATION_OFF" : "SOLVER_DEACTIVATION_LOW", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Solver Deactivation", enumVal); //SOLVER_DEACTIVATION_LOW

        QModelIndex shapeLink = nif->getIndex( rigidBody, "Shape" );
        QModelIndex shape = nif->getBlock( nif->getLink( shapeLink ) );

        nif->setLink( shapeLink, nif->getBlockNumber( iBLS ) );

        if ( shape.isValid() ) {
            // cheaper than calling spRemoveBranch
            nif->removeNiBlock( nif->getBlockNumber( shape ) );
        }


        QModelIndex moppTreeLink = nif->getIndex( rigidBody, "Shape" );
        QModelIndex ibhkMoppBvTreeShape = nif->getBlock( nif->getLink( moppTreeLink ) );
        if(ibhkMoppBvTreeShape.isValid())
        {
            enumVal = NifValue::enumOptionValue("MoppDataBuildType", "BUILT_WITHOUT_CHUNK_SUBDIVISION", &ok);
            if(ok)
                nif->set<quint8>(ibhkMoppBvTreeShape, "Build Type", enumVal);

            QModelIndex iCodeOrigin = nif->getIndex( ibhkMoppBvTreeShape, "Origin" );
            nif->set<Vector3>( iCodeOrigin, origin );

            QModelIndex iCodeScale = nif->getIndex( ibhkMoppBvTreeShape, "Scale" );
            nif->set<float>( iCodeScale, scale );

            QModelIndex iCodeSize = nif->getIndex( ibhkMoppBvTreeShape, "MOPP Data Size" );
            QModelIndex iCode = nif->getIndex( ibhkMoppBvTreeShape, "MOPP Data" );

            if ( iCodeSize.isValid()) {
                nif->set<int>( iCodeSize, moppCode.size() );
                nif->updateArray( iCode );
                QModelIndex iChild = iCode.child(0,0);
                if(iChild.isValid())
                    nif->set<QByteArray>( iChild, moppCode );
            }
        }

        Message::info( nullptr, Spell::tr( "Created v-hacd with %1 convex surfaces" ).arg( shapeList.count() ));

        // returning iCVS here can crash NifSkope if a child array is selected
        return index;
    }

    //! Combines meshes a and b ( a += b )
    void combine( NifModel * nif, const QModelIndex& lTri, QVector<float> &points, QVector<uint32_t>& triangles)
    {
        QModelIndex iData = nif->getBlock( nif->getLink( lTri, "Data" ), "NiTriBasedGeomData" );

        if ( !iData.isValid() ) return;

        QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
        Vector3 trans = nif->get<Vector3>( iData, "Translation" );
        nif->getParent(iData);

        uint32_t numVert = points.count()/3;

        for ( auto v : verts ) {
            points.append(v[0] + trans[0]);
            points.append(v[1] + trans[1]);
            points.append(v[2] + trans[2]);
        }

        QVector<Triangle> tris = nif->getArray<Triangle>(iData, "Triangles");
        // Add triangles
        for(auto t : tris)
        {
            triangles.append(t[0]+ numVert);
            triangles.append(t[1]+ numVert);
            triangles.append(t[2]+ numVert);
        }
    }
};

REGISTER_SPELL( spCreateCombinedHACD )

VHacdDialog::VHacdDialog(QWidget * parent): QDialog(parent)
{
    // those will be filled with the CVS data
    QVBoxLayout * vbox = new QVBoxLayout;
    setLayout( vbox );

    vbox->addWidget( new QLabel( Spell::tr( "Resolution" ) ) );

    paramRes = new QSpinBox;
    paramRes->setRange(10000, 64000000);
    paramRes->setSingleStep(100000);
    vbox->addWidget(paramRes);

    vbox->addWidget( new QLabel( Spell::tr( "Min Volume Percent Error Allowed" ) ) );

    paramErr = new QDoubleSpinBox();
    paramErr->setRange( 0.001, 10.0 );
    paramErr->setDecimals( 3 );
    paramErr->setSingleStep( 0.01 );
    vbox->addWidget( paramErr );

    vbox->addWidget( new QLabel( Spell::tr( "Nax Number of Convex Hulls" ) ) );

    paramMaxch = new QSpinBox;
    paramMaxch->setRange(1, 100000);
    paramMaxch->setSingleStep(1);
    vbox->addWidget(paramMaxch);

    vbox->addWidget( new QLabel( Spell::tr( "Max Number of Hull Vertices" ) ) );

    paramVpch = new QSpinBox;
    paramVpch->setRange(8, 2048);
    paramVpch->setSingleStep(1);
    vbox->addWidget(paramVpch);

    vbox->addWidget( new QLabel( Spell::tr( "Fill Method" ) ) );

    auto strings = QStringList{"Floodfill", "Surface", "Raycast"};
    paramFill = new QComboBox();
    paramFill->addItems(strings);
    vbox->addWidget(paramFill);

    vbox->addWidget( new QLabel( Spell::tr( "Material" ) ) );

    strings = NifValue::enumOptions("SkyrimHavokMaterial");
    paramMatls = new QComboBox();
    paramMatls->addItems(strings);
    vbox->addWidget(paramMatls);

    //vbox->addWidget( new QLabel( Spell::tr( "Static Object" ) ) );

    paramStatic = new QCheckBox("Static Object");
    paramStatic->setChecked(false);
    vbox->addWidget(paramStatic);

    QHBoxLayout * hbox = new QHBoxLayout;
    vbox->addLayout( hbox );

    QPushButton * okButton = new QPushButton;
    okButton->setText( Spell::tr( "Ok" ) );
    hbox->addWidget( okButton );

    QPushButton * cancel = new QPushButton;
    cancel->setText( Spell::tr( "Cancel" ) );
    hbox->addWidget( cancel );

    QObject::connect( okButton, &QPushButton::clicked, this, &VHacdDialog::accept );
    QObject::connect( cancel, &QPushButton::clicked, this, &VHacdDialog::reject );
}

void VHacdDialog::setParams(const DialValues values)
{
    paramRes->setValue(values.resolution);
    paramMaxch->setValue(values.maxConvexHulls);
    paramErr->setValue(values.minimumVolumePercentErrorAllowed);
    paramVpch->setValue(values.maxNumVerticesPerCH);
    paramFill->setCurrentIndex(static_cast<uint8_t>(values.fillMethod));
    paramStatic->setChecked(values.staticCollision);
    paramMatls->setCurrentIndex(values.matlsIndex);
}

VHacdDialog::DialValues VHacdDialog::getParams()
{
    auto method = static_cast<DialValues::FillMethod>(paramFill->currentIndex());
    DialValues retVal{(uint32_t)paramRes->value(),
                      (uint32_t)paramMaxch->value(),
                      paramErr->value(),

                      (uint32_t)paramVpch->value(),
                      paramMatls->currentIndex(),
                      paramStatic->isChecked(),
                      method

    };

    return retVal;
}



#endif
