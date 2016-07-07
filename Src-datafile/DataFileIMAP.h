#ifndef DATAFILEIMAP_H
#define DATAFILEIMAP_H

#include "DataFile.h"

/* ---------------------------------------------------------------- */
/* Types ---------------------------------------------------------- */
/* ---------------------------------------------------------------- */

class DataFileIMAP : public DataFile
{
private:
    // Input mode
    int     imCumTypCnt[CimCfg::imNTypes];
    IMROTbl roTbl;

public:
    virtual QString typeFromObj() const     {return "imec";}
    virtual QString subtypeFromObj() const  {return "imec.ap";}

    // ---------
    // Meta data
    // ---------

    int origID2Type( int ic ) const;
    double origID2Gain( int ic ) const;
    ChanMapIM chanMap() const;

protected:
    virtual void subclassParseMetaData();
    virtual void subclassStoreMetaData( const DAQ::Params &p );
    virtual int subclassGetAcqChanCount( const DAQ::Params &p );
    virtual int subclassGetSavChanCount( const DAQ::Params &p );

    virtual void subclassSetSNSChanCounts(
        const DAQ::Params   *p,
        const DataFile      *dfSrc );

private:
    void parseChanCounts();
};

#endif  // DATAFILEIMAP_H


