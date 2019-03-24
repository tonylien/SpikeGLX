
#ifdef HAVE_NIDAQmx

#include "CniAcqDmx.h"
#include "Util.h"
#include "Subset.h"

#include <QThread>

//#define PERFMON
#ifdef PERFMON
#include <windows.h>
#include <psapi.h>
#endif


#define DAQ_TIMEOUT_SEC     2.5

#define DAQmxErrChk(functionCall)                           \
    do {                                                    \
    if( DAQmxFailed(dmxErrNum = (functionCall)) )           \
        {dmxFnName = STR(functionCall); goto Error_Out;}    \
    } while( 0 )

#define DAQmxErrChkNoJump(functionCall)                     \
    (DAQmxFailed(dmxErrNum = (functionCall)) &&             \
    (dmxFnName = STR(functionCall)))


/* ---------------------------------------------------------------- */
/* Statics -------------------------------------------------------- */
/* ---------------------------------------------------------------- */

static QVector<char>    dmxErrMsg;
static const char       *dmxFnName;
static int32            dmxErrNum;

/* ---------------------------------------------------------------- */
/* clearDmxErrors ------------------------------------------------- */
/* ---------------------------------------------------------------- */

static void clearDmxErrors()
{
    dmxErrMsg.clear();
    dmxFnName   = "";
    dmxErrNum   = 0;
}

/* ---------------------------------------------------------------- */
/* lastDAQErrMsg -------------------------------------------------- */
/* ---------------------------------------------------------------- */

// Capture latest dmxErrNum as a descriptive C-string.
// Call as soon as possible after offending operation.
//
static void lastDAQErrMsg()
{
    const int msgbytes = 2048;
    dmxErrMsg.resize( msgbytes );
    dmxErrMsg[0] = 0;
    DAQmxGetExtendedErrorInfo( &dmxErrMsg[0], msgbytes );
}

/* ---------------------------------------------------------------- */
/* destroyTask ---------------------------------------------------- */
/* ---------------------------------------------------------------- */

static void destroyTask( TaskHandle &taskHandle )
{
    if( taskHandle ) {
        DAQmxStopTask( taskHandle );
        DAQmxClearTask( taskHandle );
        taskHandle = 0;
    }
}

/* ---------------------------------------------------------------- */
/* aiChanString --------------------------------------------------- */
/* ---------------------------------------------------------------- */

// Compose NIDAQ channel string of form "/dev6/ai4, ...".
//
// Return channel count.
//
static int aiChanString(
    QString             &str,
    const QString       &dev,
    const QVector<uint> &chnVec )
{
    str.clear();

    int nc = chnVec.size();

    if( nc ) {
        QString basename = QString("/%1/ai").arg( dev );
        str = QString("%1%2").arg( basename ).arg( chnVec[0] );
        for( int ic = 1; ic < nc; ++ic )
            str += QString(", %1%2").arg( basename ).arg( chnVec[ic] );
    }

    return nc;
}

/* ---------------------------------------------------------------- */
/* diChanString --------------------------------------------------- */
/* ---------------------------------------------------------------- */

// Compose NIDAQ channel string of form "/dev6/line4,...".
//
static void diChanString(
    QString             &str,
    const QString       &dev,
    const QVector<uint> &chnVec )
{
    str.clear();

    int nc = chnVec.size();

    if( nc ) {
        QString basename = QString("/%1/line").arg( dev );
        str = QString("%1%2").arg( basename ).arg( chnVec[0] );
        for( int ic = 1; ic < nc; ++ic )
            str += QString(", %1%2").arg( basename ).arg( chnVec[ic] );
    }
}

/* ---------------------------------------------------------------- */
/* ~CniAcqDmx ----------------------------------------------------- */
/* ---------------------------------------------------------------- */

CniAcqDmx::~CniAcqDmx()
{
    setDO( false );
    destroyTasks();
}

/* ---------------------------------------------------------------- */
/* run ------------------------------------------------------------ */
/* ---------------------------------------------------------------- */

/*  DAQ Strategy
    ------------
    (1) Task Configuration

    DAQmx will be configured for triggered+buffered input using
    DAQmxCfgSampClkTiming API. The interesting parameters:

    - source + activeEdge:
    An external train of clock pulses is applied here and we'll set
    the rising edges to command acquisition of one sample from each
    listed ai channel. This triggering clock signal is typically
    generated by the muxing micro-controller. The identical signal
    must be applied to each participating NI device for proper
    synchronization. This signal determines the acquisition rate.
    The 'rate' function parameter is ONLY used for buffer sizing
    (see below).

    - sampleMode:
    Set DAQmx_Val_ContSamps for continuous buffered acq on every
    rising edge.

    - sampsPerChan:
    Buffer size is specified in units of samplesPerChan. We must
    guess a worst-case latency: by how many seconds might the sample
    fetching thread lag? The desired size is then:

        maxSampPerChan      = latency-secs * samples/sec.
        maxMuxedSampPerChan = kmux * maxSampPerChan.

    In practice we find interruptions as long as a second on an older
    XP workstation.

    To prevent NI overriding our desired size, we reassert our choice
    using DAQmxCfgInputBuffer().

    (2) Sample Fetching
    The read operations offer two usage modes (both needed):

    - Get a specified number of integral samples.
    - Get all available integral samples.

    We'll be calling the readers quasi-periodically to retrieve
    samples but because of potential latency we cannot use a fixed
    request size. Rather, we will always ask for everything from the
    first reader to prevent buffer overflow. Subsequent readers use
    the actual count read from the first. Although more samples may
    have arrived by the time we get to subsequent readers, because
    there is one common clock, the count is also tracked by the first
    reader, so we'll get those samples in the next read cycle.

    The read function is nice about delivering back whole 'samples'
    but remember that the number of data points in a NIDAQ sample
    is effectively characterized as:

        Nmuxer = # physical NIDAQ lines.

    This does not account for muxing. Rather, we must maintain
    whole timepoints, each having:

        Nchan = Nmuxer * channels/muxer.

    The read function knows nothing of muxing so may well deliver data
    for partial timepoints. It will fall to us to reassemble timepoints
    manually. I'll manage that in a simple way, sizing a fetch buffer to
    hold maxMuxedSampPerChan, plus 1 extra timepoint. On each read I'll
    track any fractional timepoint tail. On the next read we will slide
    that fraction forward and append new fetched data to that.
*/

void CniAcqDmx::run()
{
// ---------
// Configure
// ---------

    if( !configure() )
        return;

// -----
// Start
// -----

    atomicSleepWhenReady();

    if( !startTasks() ) {
        runError();
        return;
    }

    if( p.ni.startEnable )
        setDO( true );

// ---
// Run
// ---

// daqAIFetchPeriodMillis set to 1 ms for release builds,
// which would give us a latency of about 1 ms. Profiling
// for the USB-6366 (slower than PCI/PXI) shows typical
// loop processing time without digital lines is ~0.1 ms
// and 1 to 2 ms with digital.

    const int loopPeriod_us =
        1000
        * daqAIFetchPeriodMillis()
        * (kxd1+kxd2 ? 2 : 0.1);

    double  peak_loopT  = 0;
    int32   nFetched;
    int     peak_nWhole = 0,
            nWhole      = 0,
            rem         = 0,
            remFront    = true,
            nTries      = 0;

    while( !isStopped() ) {

        double  loopT = getTime();

        nWhole = 0;

        // Slide partial timepoint forward.
        //
        // The purpose of remFront is to prevent sliding again
        // next time, just in case we got no samples this time.
        // In reality, if we actually had any rem (meaning acq
        // is in progress) and we got no samples, then we have
        // much bigger problems than that.

        if( rem && !remFront ) {

            slideRemForward( rem, nFetched );
            remFront = true;
        }

        // -----
        // Fetch
        // -----

        if( !fetch( nFetched, rem ) )
            goto Error_Out;

// Experiment to report fetched sample count vs time.
#if 0
{
    static double q0 = getTime();
    static QFile f;
    static QTextStream ts( &f );
    double qq = getTime() - q0;
    if( qq >= 5.0 && qq < 10.0 ) {
        if( !f.isOpen() ) {
            f.setFileName( "pace.txt" );
            f.open( QIODevice::WriteOnly | QIODevice::Text );
        }
        ts<<QString("%1\t%2\n").arg( qq ).arg( nFetched/kmux );
        if( qq >= 10.0 )
            f.close();
    }
}
#endif

        if( !nFetched )
            goto next_fetch;

        // ---------------
        // Update counters
        // ---------------

        nFetched += rem;
        nWhole    = nFetched / kmux;

        if( nWhole ) {
            rem         = nFetched - kmux * nWhole;
            remFront    = false;
        }
        else {
            rem         = nFetched;
            remFront    = true;
        }

        // ---------
        // MEM usage
        // ---------

#ifdef PERFMON
{
    static double   lastMonT = 0;
    if( loopT - lastMonT > 0.1 ) {
        PROCESS_MEMORY_COUNTERS info;
        GetProcessMemoryInfo( GetCurrentProcess(), &info, sizeof(info) );
        Log()
            << "nWhole= " << nWhole
            << " curMB= " << info.WorkingSetSize / (1024*1024)
            << " peakMB= " << info.PeakWorkingSetSize / (1024*1024);
        lastMonT = loopT;
    }
}
#endif

        // ---------------
        // Process samples
        // ---------------

        if( nWhole ) {

            if( nWhole > peak_nWhole )
                peak_nWhole = nWhole;

            // ---------------
            // Demux and merge
            // ---------------

            demuxMerge( nWhole );

            // -------
            // Publish
            // -------

            if( !totPts )
                owner->niQ->setTZero( loopT );

            owner->niQ->enqueue( &merged[0], nWhole );
            totPts += nWhole;
        }

        // ------------------
        // Handle empty fetch
        // ------------------

        // Allow retries in case of empty fetches. With USB
        // devices empty fetches happen routinely even at
        // high sample rates, and may have to do with packet
        // boundaries. Also, very low sample rates can cause
        // gaps. We choose 1100 retries to accommodate 0.5Hz
        // sample rate and higher. That many loop iterations
        // is still only about 1.1 seconds.

next_fetch:
        if( !nWhole ) {

            if( ++nTries > 1100 ) {
                runError( "NIReader getting no samples." );
                goto exit;
            }
        }
        else
            nTries = 0;

        // ------------------------------
        // Loop moderation and statistics
        // ------------------------------

        // Here we moderate the loop speed to make fetches more
        // or less the same size (loopPeriod_us per iteration).
        // We also generate some diagnostic timing stats. Note
        // that we measure things in microseconds for precision
        // but report in milliseconds for comprehension.

        loopT = 1e6*(getTime() - loopT);    // microsec

#if 0
        if( loopT > peak_loopT )
            peak_loopT = loopT;

        QString stats =
            QString(
            "DAQ rate S/max/millis %1/%2/%3"
            "   peak S/millis %4/%5")
            .arg( nWhole, 6 )
            .arg( maxMuxedSampPerChan, 6 )
            .arg( loopT/1000, 10, 'f', 2 )
            .arg( peak_nWhole, 6 )
            .arg( peak_loopT/1000, 10, 'f', 2 );
        Log() << stats;
#else
    Q_UNUSED( peak_loopT )
#endif

        if( loopT < loopPeriod_us )
            QThread::usleep( qMin( 0.5*(loopPeriod_us - loopT), 500.0 ) );
    }

// ----
// Exit
// ----

Error_Out:
    runError();

exit:
    setDO( false );
}

/* ---------------------------------------------------------------- */
/* createAITasks -------------------------------------------------- */
/* ---------------------------------------------------------------- */

bool CniAcqDmx::createAITasks(
    const QString   &aiChanStr1,
    const QString   &aiChanStr2 )
{
    taskAI1 = 0;
    taskAI2 = 0;

    if( aiChanStr1.isEmpty() )
        goto device2;

    if( DAQmxErrChkNoJump( DAQmxCreateTask( "TaskAI1", &taskAI1 ) )
     || DAQmxErrChkNoJump( DAQmxCreateAIVoltageChan(
                            taskAI1,
                            STR2CHR( aiChanStr1 ),
                            "",
                            p.ni.termCfg,
                            p.ni.range.rmin,
                            p.ni.range.rmax,
                            DAQmx_Val_Volts,
                            NULL ) )
     || DAQmxErrChkNoJump( DAQmxCfgSampClkTiming(
                            taskAI1,
                            (p.ni.isClock1Internal() ?
                                STR2CHR( QString("/%1/Ctr0InternalOutput")
                                    .arg( p.ni.dev1 ) ) :
                                STR2CHR( QString("/%1/%2")
                                    .arg( p.ni.dev1 )
                                    .arg( p.ni.clockLine1 ) )),
                            p.ni.srateSet,
                            DAQmx_Val_Rising,
                            DAQmx_Val_ContSamps,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxCfgInputBuffer(
                            taskAI1,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxTaskControl(
                            taskAI1,
                            DAQmx_Val_Task_Commit ) ) ) {

        return false;
    }

// BK: Note this property for future work on glitch recovery.
// BK: Also note this: DAQmx_Val_OverwriteUnreadSamps
//    DAQmxSetSampClkOverrunBehavior( taskAI1, DAQmx_Val_IgnoreOverruns );

device2:

    if( aiChanStr2.isEmpty() )
        return true;

    if( DAQmxErrChkNoJump( DAQmxCreateTask( "TaskAI2", &taskAI2 ) )
     || DAQmxErrChkNoJump( DAQmxCreateAIVoltageChan(
                            taskAI2,
                            STR2CHR( aiChanStr2 ),
                            "",
                            p.ni.termCfg,
                            p.ni.range.rmin,
                            p.ni.range.rmax,
                            DAQmx_Val_Volts,
                            NULL ) )
     || DAQmxErrChkNoJump( DAQmxCfgSampClkTiming(
                            taskAI2,
                            STR2CHR( QString("/%1/%2")
                                .arg( p.ni.dev2 )
                                .arg( p.ni.clockLine2 ) ),
                            p.ni.srateSet,
                            DAQmx_Val_Rising,
                            DAQmx_Val_ContSamps,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxCfgInputBuffer(
                            taskAI2,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxTaskControl(
                            taskAI2,
                            DAQmx_Val_Task_Commit ) ) ) {

        return false;
    }

// BK: Note this property for future work on glitch recovery.
//    DAQmxSetSampClkOverrunBehavior( taskAI2, DAQmx_Val_IgnoreOverruns );

    return true;
}

/* ---------------------------------------------------------------- */
/* createDITasks -------------------------------------------------- */
/* ---------------------------------------------------------------- */

bool CniAcqDmx::createDITasks(
    const QString   &diChanStr1,
    const QString   &diChanStr2 )
{
    taskDI1 = 0;
    taskDI2 = 0;

    if( diChanStr1.isEmpty() )
        goto device2;

    if( DAQmxErrChkNoJump( DAQmxCreateTask( "TaskDI1", &taskDI1 ) )
     || DAQmxErrChkNoJump( DAQmxCreateDIChan(
                            taskDI1,
                            STR2CHR( diChanStr1 ),
                            "",
                            DAQmx_Val_ChanForAllLines ) )
     || DAQmxErrChkNoJump( DAQmxCfgSampClkTiming(
                            taskDI1,
                            STR2CHR( QString("/%1/ai/SampleClock")
                                .arg( p.ni.dev1 ) ),
                            p.ni.srateSet,
                            DAQmx_Val_Rising,
                            DAQmx_Val_ContSamps,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxCfgInputBuffer(
                            taskDI1,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxTaskControl(
                            taskDI1,
                            DAQmx_Val_Task_Commit ) ) ) {

        return false;
    }

// BK: Note this property for future work on glitch recovery.
//    DAQmxSetSampClkOverrunBehavior( taskDI1, DAQmx_Val_IgnoreOverruns );

device2:

    if( diChanStr2.isEmpty() )
        return true;

    if( DAQmxErrChkNoJump( DAQmxCreateTask( "TaskDI2", &taskDI2 ) )
     || DAQmxErrChkNoJump( DAQmxCreateDIChan(
                            taskDI2,
                            STR2CHR( diChanStr2 ),
                            "",
                            DAQmx_Val_ChanForAllLines ) )
     || DAQmxErrChkNoJump( DAQmxCfgSampClkTiming(
                            taskDI2,
                            STR2CHR( QString("/%1/ai/SampleClock")
                                .arg( p.ni.dev2 ) ),
                            p.ni.srateSet,
                            DAQmx_Val_Rising,
                            DAQmx_Val_ContSamps,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxCfgInputBuffer(
                            taskDI2,
                            maxMuxedSampPerChan ) )
     || DAQmxErrChkNoJump( DAQmxTaskControl(
                            taskDI2,
                            DAQmx_Val_Task_Commit ) ) ) {

        return false;
    }

// BK: Note this property for future work on glitch recovery.
//    DAQmxSetSampClkOverrunBehavior( taskDI2, DAQmx_Val_IgnoreOverruns );

    return true;
}

/* ---------------------------------------------------------------- */
/* createInternalCTRTask ------------------------------------------ */
/* ---------------------------------------------------------------- */

// TaskIntCTR programs an internal pulser to run at the specified
// (programmed) sample rate. It drives all data collection when
// Whisper is not used. Input tasks access this clock by specifying
// "Ctr0InternalOutput" as their clock source.
//
bool CniAcqDmx::createInternalCTRTask()
{
    taskIntCTR = 0;

    if( !p.ni.isClock1Internal() )
        return true;

    if( DAQmxErrChkNoJump( DAQmxCreateTask( "TaskInternalClock", &taskIntCTR ) )
     || DAQmxErrChkNoJump( DAQmxCreateCOPulseChanFreq(
                            taskIntCTR,
                            STR2CHR( QString("/%1/ctr0")
                                .arg( p.ni.dev1 ) ),
                            "",
                            DAQmx_Val_Hz,
                            DAQmx_Val_Low,
                            0.0,
                            p.ni.srateSet,
                            0.5 ) )
     || DAQmxErrChkNoJump( DAQmxCfgImplicitTiming(
                            taskIntCTR,
                            DAQmx_Val_ContSamps,
                            1 ) )   // not used
     || DAQmxErrChkNoJump( DAQmxTaskControl(
                            taskIntCTR,
                            DAQmx_Val_Task_Commit ) ) ) {

        return false;
    }

    return true;
}

/* ---------------------------------------------------------------- */
/* createSyncPulserTask ------------------------------------------- */
/* ---------------------------------------------------------------- */

// TaskSyncPls programs a square wave with period 1 second and %50
// duty cycle (high 500 ms). Output appears at Ctr1InternalOutput,
// which is pin 40. That signal can be physically routed by the user
// to a channel in both the imec and nidq streams. This pulser can then
// serve to measure the effective sample rates of the streams, and as a
// cross reference for mapping events between streams.
//
bool CniAcqDmx::createSyncPulserTask()
{
    taskSyncPls = 0;

    if( p.sync.sourceIdx != DAQ::eSyncSourceNI )
        return true;

    if( DAQmxErrChkNoJump( DAQmxCreateTask( "TaskSyncPulser", &taskSyncPls ) )
     || DAQmxErrChkNoJump( DAQmxCreateCOPulseChanTime(
                            taskSyncPls,
                            STR2CHR( QString("/%1/ctr1")
                                .arg( p.ni.dev1 ) ),
                            "",
                            DAQmx_Val_Seconds,
                            DAQmx_Val_Low,
                            0.0,
                            0.5,
                            0.5 ) )
     || DAQmxErrChkNoJump( DAQmxCfgImplicitTiming(
                            taskSyncPls,
                            DAQmx_Val_ContSamps,
                            1 ) )   // not used
     || DAQmxErrChkNoJump( DAQmxTaskControl(
                            taskSyncPls,
                            DAQmx_Val_Task_Commit ) ) ) {

        return false;
    }

    return true;
}

/* ---------------------------------------------------------------- */
/* configure ------------------------------------------------------ */
/* ---------------------------------------------------------------- */

bool CniAcqDmx::configure()
{
    const double    lateSecs = 2.0; // worst expected latency

    QString aiChanStr1, aiChanStr2,
            diChanStr1, diChanStr2;
    uInt32  maxSampPerChan = uInt32(lateSecs * p.ni.srate);

    clearDmxErrors();

    kmux = (p.ni.isMuxingMode() ? p.ni.muxFactor : 1);

    maxMuxedSampPerChan = kmux * maxSampPerChan;

// ----------------------------------------
// Channel types, counts and NI-DAQ strings
// ----------------------------------------

    // Temporary use of vc
    {
        QVector<uint>   vc;

        // Primary device -------

        Subset::rngStr2Vec( vc, p.ni.uiMNStr1 );
        kmn1 = vc.size();

        Subset::rngStr2Vec( vc, p.ni.uiMAStr1 );
        kma1 = vc.size();

        Subset::rngStr2Vec( vc, p.ni.uiXAStr1 );
        kxa1 = vc.size();

        Subset::rngStr2Vec( vc,
            QString("%1,%2,%3")
                .arg( p.ni.uiMNStr1 )
                .arg( p.ni.uiMAStr1 )
                .arg( p.ni.uiXAStr1 ) );

        KAI1 = aiChanString( aiChanStr1, p.ni.dev1, vc );

        Subset::rngStr2Vec( vc, p.ni.uiXDStr1 );
        diChanString( diChanStr1, p.ni.dev1, vc );
        kxd1 = p.ni.xdBytes1;

        // Secondary device -------

        Subset::rngStr2Vec( vc, p.ni.uiMNStr2() );
        kmn2 = vc.size();

        Subset::rngStr2Vec( vc, p.ni.uiMAStr2() );
        kma2 = vc.size();

        Subset::rngStr2Vec( vc, p.ni.uiXAStr2() );
        kxa2 = vc.size();

        Subset::rngStr2Vec( vc,
            QString("%1,%2,%3")
                .arg( p.ni.uiMNStr2() )
                .arg( p.ni.uiMAStr2() )
                .arg( p.ni.uiXAStr2() ) );

        KAI2 = aiChanString( aiChanStr2, p.ni.dev2, vc );

        Subset::rngStr2Vec( vc, p.ni.uiXDStr2() );
        diChanString( diChanStr2, p.ni.dev2, vc );
        kxd2 = p.ni.xdBytes2;
    }

// To route a clock source to di/SampleClock without involving
// a trigger line on a chassis backplane, we use ai/SampleClock.
// That means that to do digital we ALWAYS want an analog task.
// In this case, we set up analog for a single arbitrary channel
// but skip the fetching of those data.

    if( !diChanStr1.isEmpty() && aiChanStr1.isEmpty() )
        aiChanStr1 = QString("/%1/ai0").arg( p.ni.dev1 );

    if( !diChanStr2.isEmpty() && aiChanStr2.isEmpty() )
        aiChanStr2 = QString("/%1/ai0").arg( p.ni.dev2 );

// ----------
// Task setup
// ----------

    if( p.ni.startEnable ) {
        setDO( false );
        QThread::msleep( 1000 );
    }

    if( !createInternalCTRTask() ) {
        runError();
        return false;
    }

    if( !createAITasks( aiChanStr1, aiChanStr2 ) ) {
        runError();
        return false;
    }

    if( !createDITasks( diChanStr1, diChanStr2 ) ) {
        runError();
        return false;
    }

    if( !createSyncPulserTask() ) {
        runError();
        return false;
    }

// -------
// Buffers
// -------

// IMPORTANT
// ---------
// Any of the raw buffers may get zero allocated size if no channels
// of that type were selected...so never access &rawXXX[0] without
// testing array size.

    merged.resize(
        maxSampPerChan*(
            kmux*(kmn1+kma1+kmn2+kma2)
            + kxa1+kxa2
            + (1 + kxd1+kxd2)/2
        ) );

    rawAI1.resize( (maxMuxedSampPerChan + kmux)*KAI1 );
    rawAI2.resize( (maxMuxedSampPerChan + kmux)*KAI2 );

    rawDI1.resize( maxMuxedSampPerChan + kmux );
    rawDI2.resize( maxMuxedSampPerChan + kmux );

    return true;
}

/* ---------------------------------------------------------------- */
/* startTasks ----------------------------------------------------- */
/* ---------------------------------------------------------------- */

// Start slaves then masters.
//
bool CniAcqDmx::startTasks()
{
    if( p.ni.isDualDevMode ) {

        if( taskDI2 && DAQmxErrChkNoJump( DAQmxStartTask( taskDI2 ) ) )
            return false;

        if( taskAI2 && DAQmxErrChkNoJump( DAQmxStartTask( taskAI2 ) ) )
            return false;
    }

    if( taskDI1 && DAQmxErrChkNoJump( DAQmxStartTask( taskDI1 ) ) )
        return false;

    if( taskAI1 && DAQmxErrChkNoJump( DAQmxStartTask( taskAI1 ) ) )
        return false;

    if( taskIntCTR && DAQmxErrChkNoJump( DAQmxStartTask( taskIntCTR ) ) )
        return false;

    if( taskSyncPls && DAQmxErrChkNoJump( DAQmxStartTask( taskSyncPls ) ) )
        return false;

    return true;
}

/* ---------------------------------------------------------------- */
/* destroyTasks --------------------------------------------------- */
/* ---------------------------------------------------------------- */

void CniAcqDmx::destroyTasks()
{
    destroyTask( taskSyncPls );
    destroyTask( taskIntCTR );
    destroyTask( taskAI1 );
    destroyTask( taskDI1 );
    destroyTask( taskAI2 );
    destroyTask( taskDI2 );
}

/* ---------------------------------------------------------------- */
/* setDO ---------------------------------------------------------- */
/* ---------------------------------------------------------------- */

void CniAcqDmx::setDO( bool onoff )
{
    QString err = CniCfg::setDO( p.ni.startLine, onoff );

    if( !err.isEmpty() )
        emit owner->daqError( err );
}

/* ---------------------------------------------------------------- */
/* slideRemForward ------------------------------------------------ */
/* ---------------------------------------------------------------- */

void CniAcqDmx::slideRemForward( int rem, int nFetched )
{
    if( KAI1 ) {
        memcpy(
            &rawAI1[0],
            &rawAI1[(nFetched-rem)*KAI1],
            rem*KAI1*sizeof(qint16) );
    }

    if( kxd1 ) {
        memcpy(
            &rawDI1[0],
            &rawDI1[nFetched-rem],
            rem*sizeof(uInt32) );
    }

    if( KAI2 ) {
        memcpy(
            &rawAI2[0],
            &rawAI2[(nFetched-rem)*KAI2],
            rem*KAI2*sizeof(qint16) );
    }

    if( kxd2 ) {
        memcpy(
            &rawDI2[0],
            &rawDI2[nFetched-rem],
            rem*sizeof(uInt32) );
    }
}

/* ---------------------------------------------------------------- */
/* fetch ---------------------------------------------------------- */
/* ---------------------------------------------------------------- */

// Fetch ALL dev1 samples, appending them to rem.
// The first used channel type on dev1 sets nFetched,
// and that specifies the fetch count for other reads.
//
// Return ok.
//
bool CniAcqDmx::fetch( int32 &nFetched, int rem )
{
    nFetched = 0;

    if( KAI1 ) {

        DAQmxErrChk(
            DAQmxReadBinaryI16(
                taskAI1,
                DAQmx_Val_Auto,
                DAQ_TIMEOUT_SEC,
                DAQmx_Val_GroupByScanNumber,
                &rawAI1[rem*KAI1],
                (maxMuxedSampPerChan+kmux-rem)*KAI1,
                &nFetched,
                NULL ) );

        if( !nFetched )
            goto exit;
    }

    if( kxd1 ) {

        DAQmxErrChk(
            DAQmxReadDigitalU32(
                taskDI1,
                (nFetched ? nFetched : DAQmx_Val_Auto),
                DAQ_TIMEOUT_SEC,
                DAQmx_Val_GroupByScanNumber,
                &rawDI1[rem],
                (maxMuxedSampPerChan+kmux-rem),
                &nFetched,
                NULL ) );

        if( !nFetched )
            goto exit;
    }

// Fetch nFetched dev2 samples
// Append to rem

    if( p.ni.isDualDevMode ) {

        int32   nFetched2;

        if( KAI2 ) {

            DAQmxErrChk(
                DAQmxReadBinaryI16(
                    taskAI2,
                    nFetched,
                    DAQ_TIMEOUT_SEC,
                    DAQmx_Val_GroupByScanNumber,
                    &rawAI2[rem*KAI2],
                    (maxMuxedSampPerChan+kmux-rem)*KAI2,
                    &nFetched2,
                    NULL ) );

            if( nFetched2 != nFetched )
                Warning() << "Detected dev2-dev1 analog phase shift.";
        }

        if( kxd2 ) {

            DAQmxErrChk(
                DAQmxReadDigitalU32(
                    taskDI2,
                    nFetched,
                    DAQ_TIMEOUT_SEC,
                    DAQmx_Val_GroupByScanNumber,
                    &rawDI2[rem],
                    (maxMuxedSampPerChan+kmux-rem),
                    &nFetched2,
                    NULL ) );

            if( nFetched2 != nFetched )
                Warning() << "Detected dev2-dev1 digital phase shift.";
        }
    }

exit:
    return true;

Error_Out:
    return false;
}

/* ---------------------------------------------------------------- */
/* demuxMerge ----------------------------------------------------- */
/* ---------------------------------------------------------------- */

// - Merge data from 2 devices.
// - Group by whole timepoints.
// - Subgroup (mn0 | mn1 |...| ma0 | ma1 |...| xa | xd).
// - Average oversampled xa chans.
// - Downsample oversampled xd and pack bytes into low-order bits.
//
void CniAcqDmx::demuxMerge( int nwhole )
{
    qint16          *dst    = &merged[0];
    const qint16    *sA1    = (rawAI1.size() ? &rawAI1[0] : 0),
                    *sA2    = (rawAI2.size() ? &rawAI2[0] : 0);
    const uInt32    *sD1    = (kxd1 ? &rawDI1[0] : 0),
                    *sD2    = (kxd2 ? &rawDI2[0] : 0);

// ----------
// Not muxing
// ----------

    if( kmux == 1 ) {

        for( int w = 0; w < nwhole; ++w ) {

            // Copy XA

            if( kxa1 ) {
                memcpy( dst, sA1, kxa1*sizeof(qint16) );
                dst += kxa1;
                sA1 += kxa1;
            }

            if( kxa2 ) {
                memcpy( dst, sA2, kxa2*sizeof(qint16) );
                dst += kxa2;
                sA2 += kxa2;
            }

            // Copy XD

            if( kxd1 + kxd2 == 0 )
                continue;

            quint16 W = 0;
            bool    F = 0;   // filling F: {0=empty,1=lsb,2=full}

            if( kxd1 == 4 ) {
                *dst++ = *sD1;
                *dst++ = *sD1 >> 16;
                ++sD1;
            }
            if( kxd1 == 3 ) {
                *dst++ = *sD1;
                W = (*sD1 >> 16) & 0xFF;
                F = 1;
                ++sD1;
            }
            else if( kxd1 == 2 ) {
                *dst++ = *sD1++;
            }
            else if( kxd1 == 1 ) {
                W = *sD1++ & 0xFF;
                F = 1;
            }

            if( kxd2 == 0 ) {

                if( F > 0 )
                    *dst++ = W;
            }
            else if( kxd2 == 1 ) {

                if( F == 0 )
                    *dst++ = *sD2++ & 0xFF;
                else
                    *dst++ = W + (*sD2++ << 8);
            }
            else if( kxd2 == 2 ) {

                if( F == 0 )
                    *dst++ = *sD2++;
                else {
                    *dst++ = W + (*sD2 << 8);
                    *dst++ = (*sD2 >> 8) & 0xFF;
                    ++sD2;
                }
            }
            else if( kxd2 == 3 ) {

                if( F == 0 ) {
                    *dst++ = *sD2;
                    *dst++ = (*sD2 >> 16) & 0xFF;
                    ++sD2;
                }
                else {
                    *dst++ = W + (*sD2 << 8);
                    *dst++ = *sD2 >> 8;
                    ++sD2;
                }
            }
            else if( kxd2 == 4 ) {

                if( F == 0 ) {
                    *dst++ = *sD2;
                    *dst++ = *sD2 >> 16;
                    ++sD2;
                }
                else {
                    *dst++ = W + (*sD2 << 8);
                    *dst++ = *sD2 >> 8;
                    *dst++ = *sD2 >> 24;
                    ++sD2;
                }
            }
        }

        return;
    }

// ------
// Muxing
// ------

// In each timepoint the muxed channels form a matrix. As acquired,
// each column is a muxer (so, ncol = kmn1 + kmn2 + kma1 + kma2).
// There are kmux rows. We will transpose this matrix so that all
// the samples from a given muxer are together. In each timepoint
// the xa values are oversampled by kmux, so we will average them.

    int     ncol    = kmn1 + kmn2 + kma1 + kma2,
            nrow    = kmux,
            ntmp    = nrow * ncol;
    vec_i16 vtmp( ntmp );

    for( int w = 0; w < nwhole; ++w ) {

        std::vector<long>   sumxa1( kxa1, 0 ),
                            sumxa2( kxa2, 0 );
        qint16              *tmp = &vtmp[0];

        for( int s = 0; s < kmux; ++s ) {

            // Fill MN, MA matrix

            if( kmn1 ) {
                memcpy( tmp, sA1, kmn1*sizeof(qint16) );
                tmp += kmn1;
                sA1 += kmn1;
            }

            if( kmn2 ) {
                memcpy( tmp, sA2, kmn2*sizeof(qint16) );
                tmp += kmn2;
                sA2 += kmn2;
            }

            if( kma1 ) {
                memcpy( tmp, sA1, kma1*sizeof(qint16) );
                tmp += kma1;
                sA1 += kma1;
            }

            if( kma2 ) {
                memcpy( tmp, sA2, kma2*sizeof(qint16) );
                tmp += kma2;
                sA2 += kma2;
            }

            // Sum XA

            for( int x = 0; x < kxa1; ++x )
                sumxa1[x] += *sA1++;

            for( int x = 0; x < kxa2; ++x )
                sumxa2[x] += *sA2++;
        }

        // Transpose and store MN, MA:
        // Original element address is [ncol*Y + X].
        // Swap roles X <-> Y and row <-> col.

        for( int iacq = 0; iacq < ntmp; ++iacq ) {

            int Y = iacq / ncol,
                X = iacq - ncol * Y;

            dst[nrow*X + Y] = vtmp[iacq];
        }

        dst += ntmp;

        // Copy XA averages

        for( int x = 0; x < kxa1; ++x )
            *dst++ = sumxa1[x] / kmux;

        for( int x = 0; x < kxa2; ++x )
            *dst++ = sumxa2[x] / kmux;

        // Copy XD

        if( kxd1 + kxd2 == 0 )
            continue;

        quint16 W = 0;
        bool    F = 0;   // filling F: {0=empty,1=lsb,2=full}

        if( kxd1 == 4 ) {
            *dst++ = *sD1;
            *dst++ = *sD1 >> 16;
            ++sD1;
        }
        if( kxd1 == 3 ) {
            *dst++ = *sD1;
            W = (*sD1 >> 16) & 0xFF;
            F = 1;
            ++sD1;
        }
        else if( kxd1 == 2 ) {
            *dst++ = *sD1++;
        }
        else if( kxd1 == 1 ) {
            W = *sD1++ & 0xFF;
            F = 1;
        }

        if( kxd2 == 0 ) {

            if( F > 0 )
                *dst++ = W;
        }
        else if( kxd2 == 1 ) {

            if( F == 0 )
                *dst++ = *sD2++ & 0xFF;
            else
                *dst++ = W + (*sD2++ << 8);
        }
        else if( kxd2 == 2 ) {

            if( F == 0 )
                *dst++ = *sD2++;
            else {
                *dst++ = W + (*sD2 << 8);
                *dst++ = (*sD2 >> 8) & 0xFF;
                ++sD2;
            }
        }
        else if( kxd2 == 3 ) {

            if( F == 0 ) {
                *dst++ = *sD2;
                *dst++ = (*sD2 >> 16) & 0xFF;
                ++sD2;
            }
            else {
                *dst++ = W + (*sD2 << 8);
                *dst++ = *sD2 >> 8;
                ++sD2;
            }
        }
        else if( kxd2 == 4 ) {

            if( F == 0 ) {
                *dst++ = *sD2;
                *dst++ = *sD2 >> 16;
                ++sD2;
            }
            else {
                *dst++ = W + (*sD2 << 8);
                *dst++ = *sD2 >> 8;
                *dst++ = *sD2 >> 24;
                ++sD2;
            }
        }

        if( kxd1 )
            sD1 += (kmux - 1);

        if( kxd2 )
            sD2 += (kmux - 1);
    }
}

/* ---------------------------------------------------------------- */
/* runError ------------------------------------------------------- */
/* ---------------------------------------------------------------- */

void CniAcqDmx::runError( const QString &err )
{
    if( DAQmxFailed( dmxErrNum ) )
        lastDAQErrMsg();

    destroyTasks();

    if( DAQmxFailed( dmxErrNum ) ) {

        QString e = QString("DAQmx Error:\nFun=<%1>\n").arg( dmxFnName );
        e += QString("ErrNum=<%1>\n").arg( dmxErrNum );
        e += QString("ErrMsg='%1'.").arg( &dmxErrMsg[0] );

        Error() << e;
        emit owner->daqError( e );
    }
    else if( !isStopped() ) {

        Error() << err;
        emit owner->daqError( err );
    }
}

#endif  // HAVE_NIDAQmx


