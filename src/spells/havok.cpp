#include "spellbook.h"

#include "spells/blocks.h"

#include "lib/nvtristripwrapper.h"
#include "lib/qhull.h"
#include "v-hacd/src/VHACD_Lib/public/VHACD.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>

#include <algorithm> // std::sort


// Brief description is deliberately not autolinked to class Spell
/*! \file havok.cpp
 * \brief Havok spells
 *
 * All classes here inherit from the Spell class.
 */

//! For Havok coordinate transforms
static const float havokConst = 7.0;

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
            return index;
        }

        /* make a convex hull from it */
        compute_convex_hull( vertsTrans, hullVerts, hullNorms, (float)precSpin->value() );

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
        nif->set<float>( iCVS, "Radius", spnRadius->value() );

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

        Message::info( nullptr, Spell::tr( "Created hull with %1 vertices, %2 normals" ).arg( convex_verts.count() ).arg( convex_norms.count() ) );

        // returning iCVS here can crash NifSkope if a child array is selected
        return index;
    }
};

REGISTER_SPELL( spCreateCVS )

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
public:
    QString name() const override final { return Spell::tr( "Create Convex Decomposition" ); }
    QString page() const override final { return Spell::tr( "Havok" ); }

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
        //QVector<Vector4> convex_verts, convex_norms;
        QDialog dlg;
        QVBoxLayout * vbox = new QVBoxLayout;
        dlg.setLayout( vbox );

        vbox->addWidget( new QLabel( Spell::tr( "Resolution" ) ) );

        QSpinBox * paramRes = new QSpinBox;
        paramRes->setRange(10000, 64000000);
        paramRes->setSingleStep(100000);
        paramRes->setValue(100000);
        vbox->addWidget(paramRes);

        vbox->addWidget( new QLabel( Spell::tr( "Concavity" ) ) );

        QDoubleSpinBox * paramConcav = new QDoubleSpinBox;
        paramConcav->setRange( 0, 1.0 );
        paramConcav->setDecimals( 4 );
        paramConcav->setSingleStep( 0.01 );
        paramConcav->setValue( 0.0025 );
        vbox->addWidget( paramConcav );

        vbox->addWidget( new QLabel( Spell::tr( "verticesPerCH" ) ) );
        QSpinBox* paramVpch = new QSpinBox;
        paramVpch->setRange(4, 1024);
        paramVpch->setSingleStep(1);
        paramVpch->setValue(1);
        vbox->addWidget(paramVpch);

        vbox->addWidget( new QLabel( Spell::tr( "Material" ) ) );
        QComboBox * paramMatls = new QComboBox;
        auto strings = NifValue::enumOptions("SkyrimHavokMaterial");
        paramMatls->addItems(strings);
        paramMatls->setCurrentIndex(4);
        vbox->addWidget(paramMatls);

        QHBoxLayout * hbox = new QHBoxLayout;
        vbox->addLayout( hbox );

        QPushButton * okButton = new QPushButton;
        okButton->setText( Spell::tr( "Ok" ) );
        hbox->addWidget( okButton );

        QPushButton * cancel = new QPushButton;
        cancel->setText( Spell::tr( "Cancel" ) );
        hbox->addWidget( cancel );

        QObject::connect( okButton, &QPushButton::clicked, &dlg, &QDialog::accept );
        QObject::connect( cancel, &QPushButton::clicked, &dlg, &QDialog::reject );

        if ( dlg.exec() != QDialog::Accepted ) {
            return index;
        }

        /* get the verts of our mesh */
        QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
        QVector<Vector3> vertsTrans;
        QVector<float> points;

        QVector<Triangle> tris = nif->getArray<Triangle>(iData, "Triangles");
        QVector<uint32_t> triangles;

        // Offset by translation of NiTriShape
        Vector3 trans = nif->get<Vector3>( index, "Translation" );
        for ( auto v : verts ) {
            vertsTrans.append( v + trans );
            points.append(v[0] + trans[0]);
            points.append(v[1] + trans[1]);
            points.append(v[2] + trans[2]);
        }

        // Add triangles
        for(auto t : tris)
        {
            triangles.append(t[0]);
            triangles.append(t[1]);
            triangles.append(t[2]);
        }

        VHACD::IVHACD* interfaceVHACD = VHACD::CreateVHACD();

        VHACD::IVHACD::Parameters params;
        params.m_resolution = paramRes->value();
        params.m_concavity = paramConcav->value();
        params.m_planeDownsampling = 4;
        params.m_convexhullDownsampling = 8;
        params.m_maxNumVerticesPerCH = paramVpch->value();

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

        enumVal = NifValue::enumOptionValue("SkyrimHavokMaterial", paramMatls->currentText(), &ok);
        if(!ok) enumVal = 0;

        for (unsigned int p = 0; p < interfaceVHACD->GetNConvexHulls(); ++p)
        {
            QVector<Vector4> hullVerts;
            QVector<Vector4> hullNorms;
            VHACD::IVHACD::ConvexHull ch;
            interfaceVHACD->GetConvexHull(p, ch);
            // Get points
            for (uint32_t i = 0; i < ch.m_nPoints; i++) {
                Vector4 v;
                v[0] = ch.m_points[3 * i];
                v[1] = ch.m_points[3 * i + 1];
                v[2] = ch.m_points[3 * i + 2];
                v /= havokScale;
                hullVerts.append(v);
            }

            TheMoppet.AddVertex(hullVerts);
            for (uint32_t i = 0; i < ch.m_nTriangles; i++) {
                Triangle t {
                    (quint16)ch.m_triangles[3 * i],
                            (quint16)ch.m_triangles[3 * i + 1],
                            (quint16)ch.m_triangles[3 * i + 2]
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

        enumVal = NifValue::enumOptionValue("SkyrimLayer", "SKYL_CLUTTER", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Layer", enumVal);

        nif->set<quint8>(rigidBody, "Flags and Part Number", 128);
        nif->set<ushort>(rigidBody, "Process Contact Callback Delay", 65535);

        enumVal = NifValue::enumOptionValue("hkMotionType", "MO_SYS_SPHERE_STABILIZED", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Motion System", enumVal);

        enumVal = NifValue::enumOptionValue("hkQualityType", "MO_QUAL_MOVING", &ok);
        if(ok) nif->set<quint8>(rigidBody, "Quality Type", enumVal); //MO_QUAL_MOVING

        enumVal = NifValue::enumOptionValue("hkSolverDeactivation", "SOLVER_DEACTIVATION_LOW", &ok);
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

#endif
