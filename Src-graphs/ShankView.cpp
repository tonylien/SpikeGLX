
#include "Util.h"
#include "ShankView.h"

#include <QMouseEvent>
#include <QScrollBar>

#ifdef Q_WS_MACX
#include <gl.h>
#include <agl.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif


/* ---------------------------------------------------------------- */
/* MACROS --------------------------------------------------------- */
/* ---------------------------------------------------------------- */

#define PTR( a )    (const void*)(a)

#define BCKCLR  0.2f
#define SHKCLR  0.9f

// Shank spanPix() = nr*rowPix plus another 2*rowPix for tips, tops.
// x-coords are in range [-1,1].
// y-coords are in range [0,spanPix()].

#define MRGPX   8
#define TAGPX   8
#define PADMRG  2
#define VLFT    -1.0f
#define VRGT    1.0f
#define WIDMAX  1.5f
#define SHKSEP  0.5f
#define COLSEP  0.5f
#define ROWSEP  0.5f

/* ---------------------------------------------------------------- */
/* ShankView ------------------------------------------------------ */
/* ---------------------------------------------------------------- */

ShankView::ShankView( QWidget *parent )
#ifdef OPENGL54
    :   QOpenGLWidget(parent),
#else
    :
#endif
        smap(0), rowPix(8), slidePos(0), sel(0), inited(false)
{
    QGLFormat   fmt;
    fmt.setSwapInterval( 0 );

    QGLWidget( fmt, parent );

    setAutoFillBackground( false );
    setUpdatesEnabled( true );
}


// In view width (V) we fit (s) shanks of width (w)
// and (s-1) spaces of width (f*w), so,
//
//  s*w + (s-1)*f*w = V,
//  w = V / (s + (s-1)*f).
//
// w no larger than WIDMAX.
//
// Same reasoning used for H pad sizing.
// V pad sizing set by GUI.
//
void ShankView::setShankMap( const ShankMap *map )
{
    dataMtx.lock();

    smap = map;
    map->inverseMap( ISM );

// ------------
// Shank sizing
// ------------

    int s = map->ns;

    shkWid = (VRGT-VLFT-2*TAGPX*(VRGT-VLFT)/width()) / (s + (s-1)*SHKSEP);

    if( shkWid > WIDMAX )
        shkWid = WIDMAX;

    hlfWid = shkWid * (s + (s-1)*SHKSEP) / 2;

    dataMtx.unlock();

    resizePads();

    setMouseTracking( true );
    setCursor( Qt::CrossCursor );

    updateNow();
}


void ShankView::setSel( int ic )
{
    sel = ic;
    updateNow();
}


// Note: makeCurrent() called automatically.
//
void ShankView::initializeGL()
{
#ifdef OPENGL54
    initializeOpenGLFunctions();
#else
    initializeGLFunctions();
#endif

    glDisable( GL_DEPTH_TEST );
    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnableClientState( GL_VERTEX_ARRAY );

    inited = true;
}


// Note: makeCurrent() called automatically.
//
void ShankView::resizeGL( int w, int h )
{
    if( !inited )
        return;

// ------------
// Coord system
// ------------

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glViewport( MRGPX, MRGPX, w - 2*MRGPX, h - 2*MRGPX );

    resizePads();
}


// Note: makeCurrent() called automatically.
//
void ShankView::paintGL()
{
// -----
// Setup
// -----

    QMutexLocker    ml( &dataMtx );

#ifdef OPENGL54
//    glClear(...);
#endif

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    setClipping();
    gluOrtho2D( VLFT, VRGT, vBot, vTop );

// -----
// Paint
// -----

    glClearColor( BCKCLR, BCKCLR, BCKCLR, 1 );
    glClear( GL_COLOR_BUFFER_BIT );

    if( !smap )
        return;

    drawTips();
    drawShks();
    drawTops();
    drawPads();
    drawSel();

// -------
// Restore
// -------
}


void ShankView::mouseMoveEvent( QMouseEvent *evt )
{
    int s, c, r;

    if( evt2Pad( s, c, r, evt ) ) {

        QMap<ShankMapDesc,uint>::iterator   it;

        it = ISM.find( ShankMapDesc( s, c, r, 1 ) );

        if( it != ISM.end() ) {

            emit( cursorOver( it.value(), evt->modifiers() & Qt::SHIFT ) );
            return;
        }
    }

    emit( cursorOver( -1, false ) );
}


void ShankView::mousePressEvent( QMouseEvent *evt )
{
    int s, c, r;

    if( evt2Pad( s, c, r, evt ) ) {

        QMap<ShankMapDesc,uint>::iterator   it;

        it = ISM.find( ShankMapDesc( s, c, r, 1 ) );

        if( it != ISM.end() ) {

            emit( lbutClicked(
                it.value(),
                (evt->modifiers() & Qt::SHIFT)
                || (evt->buttons() & Qt::RightButton) ) );
        }
    }
}


float ShankView::viewportPix()
{
    return height() - 2*MRGPX;
}


// Tips and tops each rowPix height.
//
float ShankView::spanPix()
{
    return rowPix * (2 + smap->nr);
}


void ShankView::setClipping()
{
    vTop = spanPix() - slidePos;
    vBot = vTop - viewportPix();
}


// A - D
// |   |
// B - C
//
void ShankView::resizePads()
{
    dataMtx.lock();

    if( !smap || !smap->e.size() || width() <= 0 ) {

        vR.clear();
        return;
    }

    int ne = smap->e.size(),
        nc = smap->nc;

    if( vR.size() != 8*ne )
        vR.resize( 8*ne );      // 2 float/vtx, 4 vtx/rect

    vC.fill( SColor(), 4*ne );  // 1 color/vtx, 4 vtx/rect

    pmrg    = PADMRG*(VRGT-VLFT)/width();
    colWid  = (shkWid - 2*pmrg)/(nc + (nc-1)*COLSEP);

    float   *V      = &vR[0];
    float   sStep   = shkWid*(1.0f+SHKSEP),
            cStep   = colWid*(1.0f+COLSEP),
            hPad    = rowPix/(1.0f+ROWSEP);

    int c = SHKCLR*255;

    for( int i = 0; i < ne; ++i, V += 8 ) {

        const ShankMapDesc  &E = smap->e[i];
        float               L, R, B, T;

        L = -hlfWid + sStep*E.s + pmrg + cStep*E.c;
        R = L + colWid;
        B = rowPix*(1 + E.r);
        T = B + hPad;

        V[0] = L;
        V[1] = T;
        V[2] = L;
        V[3] = B;
        V[4] = R;
        V[5] = B;
        V[6] = R;
        V[7] = T;

        if( !E.u )
            memset( &vC[4*i], c, 4*sizeof(SColor) );
    }

    dataMtx.unlock();
}


void ShankView::drawTips()
{
    int     ns = smap->ns,
            nv = 3 * ns,
            nf = 2 * nv;
    float   lf = -hlfWid,
            md = shkWid/2,
            vert[nf];

    for( int i = 0; i < ns; ++i, lf += shkWid*(1.0f+SHKSEP) ) {

        float   *V = &vert[6*i];

        V[0]    = lf;
        V[1]    = rowPix;

        V[2]    = lf + md;
        V[3]    = 0.0f;

        V[4]    = lf + shkWid;
        V[5]    = rowPix;
    }

    glColor3f( SHKCLR, SHKCLR, SHKCLR );
    glPolygonMode( GL_FRONT, GL_FILL );
    glVertexPointer( 2, GL_FLOAT, 0, vert );
    glDrawArrays( GL_TRIANGLES, 0, nv );
}


void ShankView::drawShks()
{
    int     ns = smap->ns,
            nv = 4 * ns,
            nf = 2 * nv;
    float   lf = -hlfWid,
            tp = spanPix() - rowPix,
            vert[nf];

    for( int i = 0; i < ns; ++i, lf += shkWid*(1.0f+SHKSEP) ) {

        float   *V = &vert[8*i];

        V[0]    = lf;
        V[1]    = tp;

        V[2]    = lf;
        V[3]    = rowPix;

        V[4]    = lf + shkWid;
        V[5]    = rowPix;

        V[6]    = lf + shkWid;
        V[7]    = tp;
    }

    glColor3f( SHKCLR, SHKCLR, SHKCLR );
    glPolygonMode( GL_FRONT, GL_FILL );
    glVertexPointer( 2, GL_FLOAT, 0, vert );
    glDrawArrays( GL_QUADS, 0, nv );
}


void ShankView::drawTops()
{
    int c = SHKCLR*255;

    drawRect( -hlfWid, spanPix(), 2*hlfWid, rowPix, QColor( c, c, c ) );
}


void ShankView::drawPads()
{
    if( !vR.size() )
        return;

    glEnableClientState( GL_COLOR_ARRAY );

    glColorPointer( 3, GL_UNSIGNED_BYTE, 0, &vC[0] );
    glPolygonMode( GL_FRONT, GL_FILL );
    glVertexPointer( 2, GL_FLOAT, 0, &vR[0] );
    glDrawArrays( GL_QUADS, 0, vR.size()/2 );

    glDisableClientState( GL_COLOR_ARRAY );
}


// A - D
// |   |
// B - C
//
void ShankView::drawSel()
{
    if( !vR.size() )
        return;

    float   *sv     = &vR[8*sel],
            xoff    = 4*(VRGT-VLFT)/width(),
            yoff    = 4,
            vert[8];

        vert[0] = sv[0] - xoff;
        vert[1] = sv[1] + yoff;
        vert[2] = sv[2] - xoff;
        vert[3] = sv[3] - yoff;
        vert[4] = sv[4] + xoff;
        vert[5] = sv[5] - yoff;
        vert[6] = sv[6] + xoff;
        vert[7] = sv[7] + yoff;

    glLineWidth( 4.0F );

    glColor3f( 0, 0, 0 );
    glPolygonMode( GL_FRONT, GL_LINE );
    glVertexPointer( 2, GL_FLOAT, 0, vert );
    glDrawArrays( GL_QUADS, 0, 4 );

    glLineWidth( 1.0F );
}


void ShankView::drawTri(float l, float t, float w, float h, QColor c )
{
    float vert[6] = {
            l    , t,
            l+w/2, t+h,
            l+w  , t };

    glColor3f( c.redF(), c.greenF(), c.blueF() );
    glPolygonMode( GL_FRONT, GL_FILL );
    glVertexPointer( 2, GL_FLOAT, 0, vert );
    glDrawArrays( GL_TRIANGLES, 0, 3 );
}


// A - D
// |   |
// B - C
//
void ShankView::drawRect( float l, float t, float w, float h, QColor c )
{
    float vert[8] = {
            l  , t,
            l  , t-h,
            l+w, t-h,
            l+w, t };

    glColor3f( c.redF(), c.greenF(), c.blueF() );
    glPolygonMode( GL_FRONT, GL_FILL );
    glVertexPointer( 2, GL_FLOAT, 0, vert );
    glDrawArrays( GL_QUADS, 0, 4 );
}


// Coords from event record:
// [L,R] = [0,width()],
// [T,B] = [0,height()].
//
// Return true if cursor in possible pad.
//
// Note: Pad may not be implemented (used).
//
bool ShankView::evt2Pad( int &s, int &c, int &r, const QMouseEvent *evt )
{
    float   w = width();

    if( !smap || !smap->e.size() || w <= 0 )
        return false;

// To local view x-coords

    w -= 2*MRGPX;

    float   x = (VRGT-VLFT)*(evt->x()-MRGPX)/w + VLFT + hlfWid;

    if( x <= 0 )
        return false;

// Which shank and col

    float   ds = shkWid*(1.0f+SHKSEP),
            dc = colWid*(1.0f+COLSEP);

    s  = x / ds;
    x -= s*ds;

    if( x > shkWid )
        return false;

    x -= pmrg;

    if( x <= 0 )
        return false;

    c = x / dc;

    if( x > c*dc + colWid )
        return false;

// To local view y-coords

    float   y = spanPix() - (evt->y() - MRGPX + slidePos) - rowPix;

    if( y <= 0 )
        return false;

// Which row

    float   hPad = rowPix/(1.0f+ROWSEP);

    r = y / rowPix;

    if( r >= (int)smap->nr )
        return false;

    if( y > r*rowPix + hPad )
        return false;

    return true;
}


int ShankView::getSelY()
{
    if( sel < 0 || !smap || !smap->e.size() || width() <= 0 )
        return 0;

    return rowPix*(1 + smap->e[sel].r) + rowPix/(2*(1.0f+ROWSEP));
}

/* ---------------------------------------------------------------- */
/* ShankScroll ---------------------------------------------------- */
/* ---------------------------------------------------------------- */

ShankScroll::ShankScroll( QWidget *parent )
    : QAbstractScrollArea(parent)
{
    theV = new ShankView( this );

    setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    setViewport( theV );
}


void ShankScroll::setRowPix( int rPix )
{
    theV->rowPix = rPix;
    theV->resizePads();
    adjustLayout();
    scrollToSelected();
}


void ShankScroll::scrollTo( int y )
{
    theV->slidePos = y;
    verticalScrollBar()->setSliderPosition( y );
}


void ShankScroll::adjustLayout()
{
    int vh = theV->viewportPix();

    verticalScrollBar()->setPageStep( vh );
    verticalScrollBar()->setRange( 0, theV->spanPix() - vh );
    updateGeometry();
}


void ShankScroll::scrollToSelected()
{
    int sc_min  = verticalScrollBar()->minimum(),
        sc_max  = verticalScrollBar()->maximum(),
        pos     = theV->spanPix() - theV->getSelY() - theV->viewportPix()/2;

    if( pos < sc_min )
        pos = sc_min;
    else if( pos > sc_max )
        pos = sc_max;

    if( pos != theV->slidePos )
        scrollTo( pos );
    else
        theV->update();
}


void ShankScroll::resizeEvent( QResizeEvent *e )
{
    Q_UNUSED( e )

    adjustLayout();

    theV->makeCurrent();
    theV->resizeGL( theV->width(), theV->height() );

#ifdef OPENGL54
    theV->update();
#endif
}


void ShankScroll::scrollContentsBy( int dx, int dy )
{
    Q_UNUSED( dx )
    Q_UNUSED( dy )

    theV->slidePos = verticalScrollBar()->sliderPosition();
    theV->update();
}


bool ShankScroll::viewportEvent( QEvent *e )
{
    QEvent::Type    type = e->type();

    if( type == QEvent::Resize )
        return QAbstractScrollArea::viewportEvent( e );
    else
        return theV->event( e );
}


