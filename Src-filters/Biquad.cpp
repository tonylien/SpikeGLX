//
//  Biquad.cpp
//
//  Created by Nigel Redmon on 11/24/12
//  EarLevel Engineering: earlevel.com
//  Copyright 2012 Nigel Redmon
//
//  For a complete explanation of the Biquad code:
//  http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
//
//  License:
//
//  This source code is provided as is, without warranty.
//  You may copy and distribute verbatim copies of this document.
//  You may modify and use this source code to create binary code
//  for your own purposes, free or commercial.
//

#include "Biquad.h"

#include <QtGlobal>

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif


Biquad::Biquad() {
    a0      = 1.0;
    a1      = 0.0;
    a2      = 0.0;
    b1      = 0.0;
    b2      = 0.0;
    Fc      = 0.50;
    Q       = sqrt(0.5);
    G       = 0.0;
    z1      = 0.0;
    z2      = 0.0;
    type    = bq_type_lowpass;
}


Biquad::Biquad(
    int     type,
    double  Fc,
    double  Q,
    double  peakGainDB )
{
    setBiquad( type, Fc, Q, peakGainDB );
}


void Biquad::setType( int type )
{
    this->type = type;
    calcBiquad();
}


void Biquad::setQ( double Q )
{
    this->Q = Q;
    calcBiquad();
}


void Biquad::setFc( double Fc )
{
    this->Fc = Fc;
    calcBiquad();
}


void Biquad::setPeakGain( double peakGainDB )
{
    this->G = peakGainDB;
    calcBiquad();
}


void Biquad::setBiquad(
    int     type,
    double  Fc,
    double  Q,
    double  peakGainDB )
{
    this->Fc    = Fc;
    this->Q     = (Q ? Q : sqrt(0.5));
    this->G     = peakGainDB;
    setType( type );
}


void Biquad::applyBlockwiseMem(
    short   *data,
    int     maxInt,
    int     ntpts,
    int     nchans,
    int     c0,
    int     cLim )
{
    double  Y       = 1.0 / maxInt;
    int     nneural = cLim - c0;

    if( nneural != (int)vz1.size() ) {

        vz1.assign( nneural, 0 );
        vz2.assign( nneural, 0 );
    }

    for( int c = c0; c < cLim; ++c ) {

        double  z1      = vz1[c - c0],
                z2      = vz2[c - c0];
        short   *d      = &data[c],
                *dlim   = &data[c + ntpts*nchans];

        for( ; d < dlim; d += nchans ) {

            double  in  = *d * Y,
                    out = in * a0 + z1;

            z1 = in * a1 + z2 - b1 * out;
            z2 = in * a2 - b2 * out;

            *d = qBound( -maxInt, int(out * maxInt), maxInt - 1 );
        }

        vz1[c - c0] = z1;
        vz2[c - c0] = z2;
    }
}


void Biquad::apply1BlockwiseMemAll(
    short   *data,
    int     maxInt,
    int     ntpts,
    int     nchans,
    int     ichan )
{
    if( nchans != (int)vz1.size() ) {

        vz1.assign( nchans, 0 );
        vz2.assign( nchans, 0 );
    }

    double  Y       = 1.0 / maxInt,
            z1      = vz1[ichan],
            z2      = vz2[ichan];
    short   *d      = &data[ichan],
            *dlim   = &data[ichan + ntpts*nchans];

    for( ; d < dlim; d += nchans ) {

        double  in  = *d * Y,
                out = in * a0 + z1;

        z1 = in * a1 + z2 - b1 * out;
        z2 = in * a2 - b2 * out;

        *d = qBound( -maxInt, int(out * maxInt), maxInt - 1 );
    }

    vz1[ichan] = z1;
    vz2[ichan] = z2;
}


void Biquad::apply1BlockwiseMem1(
    short   *data,
    int     maxInt,
    int     ntpts,
    int     nchans,
    int     ichan )
{
    if( 1 != (int)vz1.size() ) {

        vz1.assign( 1, 0 );
        vz2.assign( 1, 0 );
    }

    double  Y       = 1.0 / maxInt,
            z1      = vz1[0],
            z2      = vz2[0];
    short   *d      = &data[ichan],
            *dlim   = &data[ichan + ntpts*nchans];

    for( ; d < dlim; d += nchans ) {

        double  in  = *d * Y,
                out = in * a0 + z1;

        z1 = in * a1 + z2 - b1 * out;
        z2 = in * a2 - b2 * out;

        *d = qBound( -maxInt, int(out * maxInt), maxInt - 1 );
    }

    vz1[0] = z1;
    vz2[0] = z2;
}


void Biquad::apply1BlockwiseNoMem(
    short   *data,
    int     maxInt,
    int     ntpts,
    int     nchans,
    int     ichan )
{
    double  Y       = 1.0 / maxInt,
            z1      = 0.0,
            z2      = 0.0;
    short   *d      = &data[ichan],
            *dlim   = &data[ichan + ntpts*nchans];

    for( ; d < dlim; d += nchans ) {

        double  in  = *d * Y,
                out = in * a0 + z1;

        z1 = in * a1 + z2 - b1 * out;
        z2 = in * a2 - b2 * out;

        *d = qBound( -maxInt, int(out * maxInt), maxInt - 1 );
    }
}


void Biquad::calcBiquad()
{
    vz1.clear();
    vz2.clear();
    z1 = 0;
    z2 = 0;

    double  norm,
            K   = tan( M_PI * Fc ),
            KK  = K * K;

    if( type <= bq_type_notch ) {

        switch( type ) {

            case bq_type_lowpass:
                norm = 1 / (1 + K/Q + KK);
                a0 = KK * norm;
                a1 = 2 * a0;
                a2 = a0;
                b1 = 2 * (KK - 1) * norm;
                b2 = (1 - K/Q + KK) * norm;
                break;

            case bq_type_highpass:
                norm = 1 / (1 + K/Q + KK);
                a0 = 1 * norm;
                a1 = -2 * a0;
                a2 = a0;
                b1 = 2 * (KK - 1) * norm;
                b2 = (1 - K/Q + KK) * norm;
                break;

            case bq_type_bandpass:
                norm = 1 / (1 + K/Q + KK);
                a0 = K/Q * norm;
                a1 = 0;
                a2 = -a0;
                b1 = 2 * (KK - 1) * norm;
                b2 = (1 - K/Q + KK) * norm;
                break;

            case bq_type_notch:
                norm = 1 / (1 + K/Q + KK);
                a0 = (1 + KK) * norm;
                a1 = 2 * (KK - 1) * norm;
                a2 = a0;
                b1 = a1;
                b2 = (1 - K/Q + KK) * norm;
                break;
        }

        return;
    }

    double  V = pow( 10, fabs( G ) / 20 );

    switch( type ) {

        case bq_type_peak:
            if( G >= 0 ) {   // boost
                norm = 1 / (1 + 1/Q * K + KK);
                a0 = (1 + V/Q * K + KK) * norm;
                a1 = 2 * (KK - 1) * norm;
                a2 = (1 - V/Q * K + KK) * norm;
                b1 = a1;
                b2 = (1 - 1/Q * K + KK) * norm;
            }
            else {  // cut
                norm = 1 / (1 + V/Q * K + KK);
                a0 = (1 + 1/Q * K + KK) * norm;
                a1 = 2 * (KK - 1) * norm;
                a2 = (1 - 1/Q * K + KK) * norm;
                b1 = a1;
                b2 = (1 - V/Q * K + KK) * norm;
            }
            break;

        case bq_type_lowshelf:
            if( G >= 0 ) {   // boost
                norm = 1 / (1 + sqrt(2.0) * K + KK);
                a0 = (1 + sqrt(2*V) * K + V * KK) * norm;
                a1 = 2 * (V * KK - 1) * norm;
                a2 = (1 - sqrt(2*V) * K + V * KK) * norm;
                b1 = 2 * (KK - 1) * norm;
                b2 = (1 - sqrt(2.0) * K + KK) * norm;
            }
            else {  // cut
                norm = 1 / (1 + sqrt(2*V) * K + V * KK);
                a0 = (1 + sqrt(2.0) * K + KK) * norm;
                a1 = 2 * (KK - 1) * norm;
                a2 = (1 - sqrt(2.0) * K + KK) * norm;
                b1 = 2 * (V * KK - 1) * norm;
                b2 = (1 - sqrt(2*V) * K + V * KK) * norm;
            }
            break;

        case bq_type_highshelf:
            if( G >= 0 ) {   // boost
                norm = 1 / (1 + sqrt(2.0) * K + KK);
                a0 = (V + sqrt(2*V) * K + KK) * norm;
                a1 = 2 * (KK - V) * norm;
                a2 = (V - sqrt(2*V) * K + KK) * norm;
                b1 = 2 * (KK - 1) * norm;
                b2 = (1 - sqrt(2.0) * K + KK) * norm;
            }
            else {  // cut
                norm = 1 / (V + sqrt(2*V) * K + KK);
                a0 = (1 + sqrt(2.0) * K + KK) * norm;
                a1 = 2 * (KK - 1) * norm;
                a2 = (1 - sqrt(2.0) * K + KK) * norm;
                b1 = 2 * (KK - V) * norm;
                b2 = (V - sqrt(2*V) * K + KK) * norm;
            }
            break;
    }
}


