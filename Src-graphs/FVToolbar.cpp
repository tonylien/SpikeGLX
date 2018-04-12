
#include "Pixmaps/apply_all.xpm"

#include "Util.h"
#include "FileViewerWindow.h"
#include "FVToolbar.h"
#include "SignalBlocker.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QAction>
#include <QLabel>




FVToolbar::FVToolbar( FileViewerWindow *fv, int fType ) : fv(fv)
{
    QDoubleSpinBox  *S;
    QSpinBox        *V;
    QPushButton     *B;
    QCheckBox       *C;
    QComboBox       *CB;
    QAction         *A;
    QLabel          *L;

// Sort selector

    B = new QPushButton( this );
    B->setObjectName( "sortbtn" );
    B->setToolTip( "Toggle graph sort order: user/acquired" );
    ConnectUI( B, SIGNAL(clicked()), fv, SLOT(tbToggleSort()) );
    addWidget( B );

// Selected

    A = new QAction( "", this );
    A->setObjectName( "nameact" );
    A->setToolTip( "Selected graph (click to find)" );
    A->setFont( QFont( QApplication::font().family(), 10, QFont::DemiBold ) );
    ConnectUI( A, SIGNAL(triggered(bool)), fv, SLOT(tbScrollToSelected()) );
    addAction( A );

// X-Scale

    addSeparator();

    L = new QLabel( "Secs", this );
    addWidget( L );

    S = new QDoubleSpinBox( this );
    S->setObjectName( "xscalesb" );
    S->setToolTip( "Scan much faster with short span ~1sec" );
    S->setDecimals( 4 );
    S->setRange( 0.0001, qMin( 30.0, fv->tbGetfileSecs() ) );
    S->setSingleStep( 0.25 );
    S->setValue( fv->tbGetxSpanSecs() );
    ConnectUI( S, SIGNAL(valueChanged(double)), fv, SLOT(tbSetXScale(double)) );
    addWidget( S );

// YPix

    addSeparator();

    L = new QLabel( "YPix", this );
    addWidget( L );

    V = new QSpinBox( this );
    V->setObjectName( "ypixsb" );
    V->setToolTip( "Height on screen (all graphs)" );
    V->setMinimum( 4 );
    V->setMaximum( 500 );
    V->setValue( fv->tbGetyPix() );
    ConnectUI( V, SIGNAL(valueChanged(int)), fv, SLOT(tbSetYPix(int)) );
    addWidget( V );

// YScale

    L = new QLabel( "YScale", this );
    addWidget( L );

    S = new QDoubleSpinBox( this );
    S->setObjectName( "yscalesb" );
    S->setToolTip( "Y magnifier (sel graph)" );
    S->setRange( 0.0, 999.0 );
    S->setSingleStep( 0.25 );
    S->setValue( fv->tbGetyScl() );
    ConnectUI( S, SIGNAL(valueChanged(double)), fv, SLOT(tbSetYScale(double)) );
    addWidget( S );

// Gain

    L = new QLabel( "Gain", this );
    addWidget( L );

    S = new QDoubleSpinBox( this );
    S->setObjectName( "gainsb" );
    S->setToolTip( "Amplifier gain (sel graph)" );
    S->setDecimals( 3 );
    S->setRange( 0.001, 1e6 );
    ConnectUI( S, SIGNAL(valueChanged(double)), fv, SLOT(tbSetMuxGain(double)) );
    addWidget( S );

// NDivs

    addSeparator();

    L = new QLabel( "NDivs", this );
    addWidget( L );

    V = new QSpinBox( this );
    V->setToolTip( "Ruler (all graphs)" );
    V->setMinimum( 1 );
    V->setMaximum( 10 );
    V->setValue( fv->tbGetNDivs() );
    ConnectUI( V, SIGNAL(valueChanged(int)), fv, SLOT(tbSetNDivs(int)) );
    addWidget( V );

    L = new QLabel( " Boxes - x -", this );
    L->setObjectName( "divlbl" );
    addWidget( L );

// Hipass

    addSeparator();

    if( fType == 2 ) {

        C = new QCheckBox( "300 - INF", this );
        C->setToolTip( "Applied only to neural channels" );
        C->setChecked( fv->tbGet300HzOn() );
        ConnectUI( C, SIGNAL(clicked(bool)), fv, SLOT(tbHipassClicked(bool)) );
        addWidget( C );
    }

// -<T> (DC filter)

    C = new QCheckBox( "-<T>", this );
    C->setToolTip( "Temporally average neural channels" );
    C->setChecked( fv->tbGetDCChkOn() );
    ConnectUI( C, SIGNAL(clicked(bool)), fv, SLOT(tbDcClicked(bool)) );
    addWidget( C );

// -<S> (spatial average)

    if( fType != 1 ) {

        L = new QLabel( "-<S>", this );
        L->setTextFormat( Qt::PlainText );
        L->setToolTip( "Spatially average spike channels" );
        L->setStyleSheet( "padding-bottom: 1px" );
        addWidget( L );

        CB = new QComboBox( this );
        CB->setToolTip( "Spatially average spike channels" );
        CB->addItem( "Off" );
        CB->addItem( "Loc 1,2" );
        CB->addItem( "Loc 2,8" );
        CB->addItem( "Glb All" );
        CB->addItem( "Glb Dmx" );
        CB->setCurrentIndex( fv->tbGetSAveSel() );
        ConnectUI( CB, SIGNAL(currentIndexChanged(int)), fv, SLOT(tbSAveSelChanged(int)) );
        addWidget( CB );
    }

// BinMax

    if( fType != 1 ) {

        C = new QCheckBox( "BinMax", this );
        C->setToolTip( "Graph extrema in each spike channel downsample bin" );
        C->setStyleSheet( "padding-left: 4px" );
        C->setChecked( fv->tbGetBinMaxOn() );
        ConnectUI( C, SIGNAL(clicked(bool)), fv, SLOT(tbBinMaxClicked(bool)) );
        addWidget( C );
    }

// Apply all

    addSeparator();

    addAction(
        QIcon( QPixmap( apply_all_xpm ) ),
        "Apply selected graph settings to all graphs of like type",
        fv, SLOT(tbApplyAll()) );
}


void FVToolbar::setSortButText( const QString &name )
{
    QPushButton *B = findChild<QPushButton*>( "sortbtn" );

    B->setText( name );
}


void FVToolbar::setSelName( const QString &name )
{
    QAction *A = findChild<QAction*>( "nameact" );

    A->setText( name );
}


void FVToolbar::setXScale( double secs )
{
    QDoubleSpinBox  *XS = findChild<QDoubleSpinBox*>( "xscalesb" );

    SignalBlocker   b0(XS);

    XS->setValue( secs );
}


void FVToolbar::enableYPix( bool enabled )
{
    QSpinBox    *V = findChild<QSpinBox*>( "ypixsb" );

    V->setEnabled( enabled );
}


void FVToolbar::setYSclAndGain( double yScl, double gain, bool enabled )
{
    QDoubleSpinBox  *YS = findChild<QDoubleSpinBox*>( "yscalesb" );
    QDoubleSpinBox  *GN = findChild<QDoubleSpinBox*>( "gainsb" );

    SignalBlocker   b0(YS), b1(GN);

    YS->setValue( yScl );
    GN->setValue( gain );

    YS->setEnabled( enabled );
    GN->setEnabled( enabled );
}


void FVToolbar::setNDivText( const QString &s )
{
    QLabel  *L = findChild<QLabel*>( "divlbl" );

    L->setText( s );
}


