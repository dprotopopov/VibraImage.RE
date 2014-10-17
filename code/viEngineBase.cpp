#include "StdAfx.h"
#include "VIEngine.h"
#include "vienginebase.h"
#include "vienginesimple.h"
#include <math.h>
#include <atlimage.h>
#include "round.h"

/// <summary>
/// Освобождение памяти, переданной указателем на элемент с предварительной проверкой значения указателя на ненулевое значение.
/// По завершении значение указателя устанавливается равным нулю.
/// </summary>
/// <param name="p">Указадель на элемент, ранее аллокированный оператором new</param>
#define SAFE_DELETE(p)       { if(p) { delete (p);     (p)=NULL; } } 
/// <summary>
/// Освобождение памяти, переданной указателем на массив элементов с предварительной проверкой значения указателя на ненулевое значение.
/// По завершении значение указателя устанавливается равным нулю.
/// </summary>
/// <param name="p">Указадель на элемент, ранее аллокированный оператором new[]</param>
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=NULL; } }

/// <summary>
/// </summary>
/// <param name=""></param>
CVIEngineBase::CVIEngineBase(int nThread) :
m_cfg(this)
,m_bInit(false)
,m_nThreadsRqst(nThread)
,m_audio(this)
,m_stat2(this)
,m_statAVG(this)
,m_statFFT(this)
,m_fpsIn(this)
,m_fpsOutF(this)
,m_fpsOutR(this)
,m_fpsDropR(this)
,m_fpsDropF(this)
#ifndef SEQ_DISABLE_LD
,m_statLDF(this)
#endif
#ifndef SEQ_DISABLE_FN
,m_statFn(this)
#endif
,m_procF6(this,VI_VAR_STAT_RES_F6,VI_VAR_STAT_RES_F8,VI_VAR_STAT_RES_F7,VI_FILTER_BWT_F6_HI,VI_FILTER_BWT_F6_LO,VI_FILTER_F6_N,VI_VAR_STAT_RES_F9)
#ifndef SEQ_DISABLE_FACE
,m_pFace(0)
#endif
#ifndef SEQ_DISABLE_DISTORTION
,m_Distortion(this)
#endif
{
    CoInitialize(0);

	// Инициализация функции-генератора псевдослучайных значений rand начальной величиной
	// Выбор значения инициализации желательно делать как можно случайным.
	// В данном случае инициализация проводится текущим значением количества тиков системных часов
	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	srand(GetTickCount());

    m_bDone = m_bStop = m_bLock = 0;
    m_nMake = 0;
    ZeroMemory(m_cMake,sizeof(m_cMake));

    m_hThreadAddImage = 0;
    m_hThreadAddImage8 = 0;

    CallbackOnNewVarData = 0;
    CallbackOnNewVar = 0;
    CallbackOnImg8 = 0;
    CallbackOnImg8Data = 0;

    m_tVideoDT = 0;
    m_tVideoT = 0;
    m_tVideoTPrev = 0;
    m_tVideoPrev = 0;

    m_resultSize.cx = m_resultSize.cy = 0;
    m_resultVer = 1;

    m_vPos.m_pBase = this;

    SEQ_XOR_INIT(this);

    m_nThreads = GetOptimalThreadCount();
    ZeroMemory(m_therads,sizeof(m_therads));
    MakeDefPal(m_palI);

    SEQ_GUARDANT_INIT(this);

    m_tMakeImage = m_timer.Get();

    SEQ_INET_STEP01A(this);
    m_bInit = true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
CVIEngineBase::~CVIEngineBase(void)
{
    Stop();
}

/// <summary>
/// </summary>
/// <param name="bLock"></param>
void CVIEngineBase::CreateThreads(bool bLock)
{
    CMTSingleLock   lock(m_locks + LVI_ALL, bLock);
    m_bDone = 0;

    int w,h,k;
    m_cfg.GetI(VI_VAR_SIZE,w,h);

    int step = h / m_nThreads;
    int nSum = m_summ.size();


    for( k = 0; k < m_nThreads; ++k)
    {
        m_therads[k] = new CVIEngineThread(this);

        m_therads[k]->m_yS = k*step;
        m_therads[k]->m_yE = (k+1)*step;
        if(m_therads[k]->m_yE > h)
            m_therads[k]->m_yE = h;

        m_therads[k]->Start();
        SetThreadPriority(m_therads[k]->m_hThread, THREAD_PRIORITY_ABOVE_NORMAL);
    }

    for( k = 0; k < nSum; ++k )
    {
        int th = k % m_nThreads;
        m_therads[th]->m_nSum.push_back(k);
    }

    lock.Unlock();

    m_hThreadAddImage = CreateThread(0,0,AddImageThread,this,0,0);
    m_hThreadAddImage8 = CreateThread(0,0,AddImageThread8,this,0,0);

    SetThreadPriority(m_hThreadAddImage8,THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(m_hThreadAddImage,THREAD_PRIORITY_ABOVE_NORMAL);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::CloseThreads(bool bLock)
{
    m_cfg.PutI1(VI_FILTER_PAUSE,1);

    m_events[EVI_DONE].Set();
    if(m_therads[0] && m_therads[0]->m_hThread)
        Sleep(1000);

    CMTSingleLock   slock(m_locks + LVI_SRC, true);
    m_srcF.clear();
    slock.Unlock();

    int nWait = 0;
    while(m_nMake && nWait < 100)
    {
        Sleep(10);
        ++nWait;
    }

    int cnt = sizeof(m_therads)/sizeof(LPVOID),k;
    if(m_nMake)
    {
        for(k = 0; k < cnt; ++k)
        {
            if(m_therads[k])
                m_therads[k]->m_evReady.Set();
        }
    }

    CMTSingleLock   lock(m_locks + LVI_ALL, bLock);

    Make(EVI_DONE);

    m_bDone = 2;

    Sleep(50);

    for(k = 0; k < cnt; ++k)
    {
        if(m_therads[k])
        {
            delete m_therads[k];
            m_therads[k] = 0;
        }
    }

    while(m_hThreadAddImage || m_hThreadAddImage8)
        Sleep(10);

    m_events[EVI_DONE].Reset();

    lock.Unlock();
    m_bDone = 0;
    m_cfg.PutI1(VI_FILTER_PAUSE,0);
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::AddImage8()
{
    if(!m_cfg.GetI1(VI_VAR_NFRAME_IN))
        return false;

	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	DWORD tS = GetTickCount(), tE, tD;  // Время начала работы данного процесса, время его завершения работы, продолжительность его работы

    int w = m_imgSrc8.w;
    int h = m_imgSrc8.h;
    BYTE *p = m_imgSrc8.p;

    SEQ_XOR_SYNC(this);
    if( m_bStop )
        return false;

    float fpsMax = m_cfg.GetF1(VI_VAR_FPSMAXF);
    BOOL bFpsDiv = (m_cfg.GetI1(VI_FILTER_FPSDIV));
    ///////////////////////////////////////////////////////
    // делитель чк
    ///////////////////////////////////////////////////////
    if(bFpsDiv)
    {

        float fpsIn = m_cfg.GetF1(VI_VAR_FPSIN);
        int div = fpsMax > 0 ? round(fpsIn/fpsMax) : 1;
        if(div > 1  || m_cfg.GetI1(VI_VAR_NFRAME_IN) < 3)
        {
            int N = m_cfg.GetI1(VI_VAR_NFRAME_IN);
            if(N%div != 0)
            {
                return false;
            }
        }
    }
    ///////////////////////////////////////////////////////

    m_cfg.PutI1(VI_VAR_FPS_BUFFER_SIZE, m_srcF.size());
    CMTSingleLock   slock(m_locks + LVI_SRC, true);
    if( !m_srcF.size() )
    {
        m_srcF.push_back( SRC_IMG() );
    }
    SRC_IMG &img = m_srcF.back();

	// Запись в свойства изображения отметки времени (в тиках с момента запуска системы)
	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	img.t = GetTickCount();
	// Вычисление количества пикселей прямоуголного изображения
    size_t size = w*h;

    img.i.resize(w,h,false,w*4);

    if( NeedSrcImageProc() )
        img.ib = m_imgSrc8;

    CMTSingleLock   lock(m_locks + LVI_SRC8, true);

    CVIEngineSimple::cvt_8i_to_32f(p,img.i.p,size,this);

    lock.Unlock();

    m_stat2.Add(img.i.p,img.i.w,img.i.h);

    float fpsOutF = m_fpsOutF.Put();
    m_cfg.PutF1(VI_VAR_FPSOUTF, min(fpsOutF,m_cfg.GetF1(VI_VAR_FPSIN)) );

    int nFrame = m_cfg.GetI1(VI_VAR_NFRAME_OUTF);
    m_cfg.PutI1(VI_VAR_NFRAME_OUTF, nFrame + 1);

    slock.Unlock();

    MakeSin();

    if(bFpsDiv)
        AddImageThreadProc();

    if(!bFpsDiv)
    {
        float fpsCur = m_cfg.GetF1(VI_VAR_FPSOUTF);

		// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
		tE = GetTickCount();
        tD = tE - tS;
        if( fpsMax > 0 && fpsCur > fpsMax)
        {
            DWORD tR = (int)(1000.0f/fpsMax);
            if( tD < tR )
            {
                DWORD dt = tR - tD;
                if(dt > 2000) dt = 2000;
                Sleep(dt);
            }
        }
    }
    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::CheckFPSDIV()
{
    if(m_cfg.GetI1(VI_FILTER_FPSDIV) == m_cfg.GetI1(VI_FILTER_FPSDIV_RQST))
        return false;
    CMTSingleLock lock(m_locks + LVI_SRC, true);

    m_srcF.clear();

    m_cfg.PutI1(VI_FILTER_FPSDIV,m_cfg.GetI1(VI_FILTER_FPSDIV_RQST));
    return true;
}

/// <summary>
/// </summary>
/// <param name="p"></param>
/// <param name="w">Ширина изображения</param>
/// <param name="h">Высота изображения</param>
/// <param name="bpp"></param>
/// <param name="t"></param>
/// <param name="nRef"></param>
bool CVIEngineBase::AddImage(void* p, int w, int h, int bpp, double t, int nRef)
{
    if(t > m_tVideo)
    {
        double dt = t - m_tVideo;
        if(m_tVideoDT > 0 && fabs( (m_tVideoDT-dt)*2/(m_tVideoDT+dt) ) > 0.5)
            m_cfg.PutI1(VI_VAR_NDROP,m_cfg.GetI1(VI_VAR_NDROP)+1);

        m_tVideoDT = dt;
    } else
        if( t > 0 && t == m_tVideo )
            return false;

        m_tVideoPrev = m_tVideo;
    m_tVideoTPrev = m_tVideoT;
    m_tVideo = t;
    m_tVideoT = m_timer.Get();

    if(p && w && h )
    {
        if(!m_timerSync.Add(t) )
            NewSource();

        m_audio.OnVideo();
    } else
        NewSource();

    if( m_summ.empty() )
    {
        return false;
    }

    switch( m_cfg.GetI1(VI_VAR_CFG_INIT)  )
    {
        default:
        case 0:
            return false;
        case 1:
            m_cfg.PutI1(VI_VAR_CFG_INIT,2);
        case 2:
            break;
    }


    CheckFPSDIV();


    CMTSingleLock   xlock(m_locks + LVI_SRC8, false);

    if(!nRef) // Crop
    {

        int cw = 0, ch = 0;
        m_cfg.GetI(VI_FILTER_CROP,cw,ch);

        cw &= (~3);
        ch &= (~3);

        if(!cw) cw = w;
        if(!ch) ch = h;

        int nw = min(cw,w);
        int nh = min(ch,h);

        nw &= (~3);
        nh &= (~3);

        if( nw != w || nh != h )
        {
            xlock.Lock();

            if( m_crop.Crop(p,w,h,bpp,nw,nh) )
            {
                xlock.Unlock();
                return AddImage(m_crop.m_ret.p,nw,nh,bpp,t,1);
            }
        } else
            m_crop.Clear();
    }
    if(p && !m_bLock)
    {
        if(m_bDone || m_bStop)
            return false;
        if( m_cfg.GetI1( VI_FILTER_PAUSE ) )
            return false;

        int nFrame = m_cfg.GetI1(VI_VAR_NFRAME_IN);
        m_cfg.PutI1(VI_VAR_NFRAME_IN, nFrame + 1);

        if(!m_cfg.GetI1(VI_FILTER_FPSDIV))
        {
            int dfr = m_cfg.GetI1(VI_FILTER_FPS2IN);

            if(!dfr && m_cfg.GetI1(VI_FILTER_AUTODOWNRATE) && m_tVideo > 5)
            {
                float fpsMax = max( m_cfg.GetF1(VI_VAR_FPSMAXR),
                                    m_cfg.GetF1(VI_VAR_FPSMAXF) );
                float fpsCur = m_fpsIn.Get();

                if(fpsMax > 0)
                    m_cfg.PutI1(VI_FILTER_FPS2IN,ceil(fpsCur/fpsMax));
            }

            if( dfr > 1 && (nFrame%dfr) != 0 )
                return false;
        }
    }

    if(!m_bLock || !p)
        CheckNRqst(w,h);
    else
        m_imgSrc8.resize(w,h,false,w);

    if(!p)
        return false;



    xlock.Lock();

    if( m_imgSrcMask.size() == m_imgSrc8.size() )
    {
        if( bpp == 24 )
            CVIEngineSimple::cvt_gs24_to_gs8_mask(
                p,m_imgSrc8.begin(),
                                                  m_imgSrc8.size(),
                                                  m_imgSrcMask.begin());
            else
                if( bpp == 8 )
                    CVIEngineSimple::mask_cpy(m_imgSrc8.begin(),
                                              p,m_imgSrc8.size(),
                                              m_imgSrcMask.begin());
    } else
    {
        if( bpp == 24 )
            CVIEngineSimple::cvt_gs24_to_gs8(p,
                                             m_imgSrc8.begin(),m_imgSrc8.size());
            else
                if( bpp == 8 )
                    SSEmemcpy(m_imgSrc8.begin(),p,m_imgSrc8.size());
    }

    if(!m_bLock)
    {
        if(!m_cfg.GetI1(VI_FILTER_FPSDIV))
            m_cfg.PutF1(VI_VAR_FPSIN,   m_fpsIn.Put() );
        else
            m_cfg.PutF1(VI_VAR_FPSIN,   m_fpsIn.PutT(round(t*1000)) );
    }

    if(CallbackOnImg8)
        CallbackOnImg8(CallbackOnImg8Data,m_imgSrc8.begin(),m_imgSrc8.w,m_imgSrc8.h,t);
    xlock.Unlock();

    if(m_cfg.GetI1(VI_FILTER_FPSDIV) && m_cfg.GetI1(VI_VAR_NFRAME_IN) < 3)
        return false;

    CMTSingleLock   xlock24(m_locks + LVI_SRC24, true);
    if( m_cfg.GetI1(VI_MODE_COLOR) )
    {
        m_imgSrc24.resize(w,h,false);
        int wh = w*h;
        if( bpp == 24 )
            SSEmemcpy(m_imgSrc24.p,p,wh*3);
        else
            CVIEngineSimple::cvt_gs8_to_gs24(p,m_imgSrc24.begin(),wh);
    } else
    {
        m_imgSrc24.clear();
    }
    xlock24.Unlock();

    if(!m_bDone && !m_bLock)
    {

        m_events[EVI_ADD8].Set();

        if(m_cfg.GetI1(VI_FILTER_FPSDIV))
            m_events[EVI_ADD8_READY].Wait(INFINITE,
                                          m_events[EVI_DONE].m_hEvent);

    }

    int rmode = m_cfg.GetI1(VI_MODE_RESULT);
    if( (rmode & VI_RESULT_SRC_MASK) || m_bLock)
    {
        CMTSingleLock   rlock(m_locks + LVI_RESULT, true);
        MakeResultSrc();
    }


    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::MakeImage(float* pFI, BYTE *pBI, int w, int h)
{
    if(m_bDone || m_bStop)
        return false;

    int cw,ch;
    m_cfg.GetI(VI_VAR_SIZE,cw,ch);
    if(cw != w || ch != h)
        return false;

    ++m_nMake;
    CMTSingleLock   lock(m_locks + LVI_ALL, true);

    if( m_cfg.GetI1(VI_VAR_RESET) )
        Reset(true);


    if(!m_cfg.GetI1(VI_VAR_NFRAME_IN))
        return false;

    m_imgSrcF = pFI;
    if(!m_bStop)
        NextSrc();

    if(!m_bStop) // 080227
        ClearStat();

    #ifndef SEQ_DISABLE_FACE
    CVIEngineFace::AddImage(this,pBI,w,h);
    #endif

    if(!m_bStop)
        Make(EVI_ADDF);

    if(!m_bStop)
        Make(EVI_DELTA);

    if(!m_bStop)
        Make(EVI_SUM);

    if(!m_bStop)
        Make(EVI_SUM_FILTER);

    #ifndef SEQ_DISABLE_FACE
    CVIEngineFace::Sync(this);
    #endif

    if(!m_bStop)
        Make(EVI_SUM_STAT);

    if(!m_bStop)
        MakeStatSum();


    if(!m_bStop && m_cfg.GetI1(VI_VAR_NFRAME) > 3)
        Make(EVI_AURA);


    MakeMotion();

    if(!m_bStop)
        StatUpdate();

    #ifndef SEQ_DISABLE_LD
    if(!m_bStop)
        m_statLDF.Make();
    #endif

    m_statRelease = m_stat;

    m_vPos.Make();

    MakeAnger();
    MakeStress();

    MakeState();

    #ifndef SEQ_DISABLE_FACE
    if(m_pFace)
        m_pFace->MakeRelease();
    #endif
    if(m_resultSize.cx == w && m_resultSize.cy == h && !m_bStop)
    {
        int rmode = m_cfg.GetI1(VI_MODE_RESULT);
        if( rmode )
        {
            CMTSingleLock   rlock(m_locks + LVI_RESULT, true);
            Make(EVI_RESULT);
            MakeFaceDraw();
            ++m_resultVer;
        }
    }

    if(!m_bStop)
    {
        m_cfg.PutF1(VI_VAR_FPSOUTR, m_fpsOutR.Put() );
        m_cfg.PutI1(VI_VAR_NFRAME,  m_cfg.GetI1(VI_VAR_NFRAME)+1);
    }

    lock.Unlock();

    --m_nMake;

    m_tMakeImage = m_timer.Get();
    //////////////////////

    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::SetSize(int w, int h, int cnt)
{
    if(m_bDone)
        return true;

    int fltRaw = w * sizeof(float);

    CloseThreads();

    m_cfg.PutI(VI_VAR_SIZE,w,h);

    m_srcMask.resize(w,h,true,fltRaw);
    m_imgSrc8.resize(w,h,true,w);

    int k;
    SetCount(0,false);
    SetCount(cnt,false);

    for( k = 0; k < (int)m_summ.size(); ++k )
    {
        int sizeY8 = h/8;

        m_summ[k].i.resize(w,h*4,true,w*sizeof(float));
        m_summ[k].si.resize(w,h*2,true,w*sizeof(short));

        m_stat[k].auraA.outlineL.resize(h);
        m_stat[k].auraA.outlineR.resize(h);

        m_stat[k].auraA.line.resize(h);
        m_stat[k].auraA.line.resize(h);

        m_stat[k].auraB.outlineL.resize(h);
        m_stat[k].auraB.outlineR.resize(h);

        m_stat[k].auraB.line.resize(h);
        m_stat[k].auraB.line.resize(h);
    }



    ClearStat();

    CreateThreads();

    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::SetCount(int cnt, bool bLock)
{
    Reset();

    CMTSingleLock   lock(m_locks + LVI_ALL, bLock);

    m_cfg.PutI1(VI_VAR_NFRAME,0);

    m_arrSrc.resize(cnt + 1);


    m_arrDelta.resize(cnt + 1);

    ilFRAME_IMG i;
    int pos;

    SIZE size;
    m_cfg.GetI(VI_VAR_SIZE,size.cx,size.cy);

    int nFrame = m_cfg.GetI1(VI_VAR_NFRAME);

    for(i = m_arrSrc.begin(), pos = 0; i != m_arrSrc.end(); ++i, ++pos)
    {
        i->n = nFrame-pos;
        i->i.resize( size.cx,size.cy,true,size.cx*sizeof(float));
    }

    for(i = m_arrDelta.begin(), pos = 0; i != m_arrDelta.end(); ++i, ++pos)
    {
        i->n = nFrame-pos;
        i->i.resize( size.cx,size.cy,true,size.cx*sizeof(float));
    }
}


/// <summary>
/// </summary>
/// <param name=""></param>
mmx_array2<float>& CVIEngineBase::NextSrc(void)
{
    ilFRAME_IMG i;

    i = m_arrSrc.end(); --i;
    m_arrSrc.splice(m_arrSrc.begin(),m_arrSrc,i);

    FRAME_IMG& img = m_arrSrc.front();
    img.n = m_cfg.GetI1(VI_VAR_NFRAME);

    i = m_arrDelta.end();   --i;
    m_arrDelta.splice(m_arrDelta.begin(),m_arrDelta,i);

    FRAME_IMG& delta = m_arrDelta.front();
    delta.n = m_cfg.GetI1(VI_VAR_NFRAME);

    return img.i;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::WaitThreads(void)
{
    int k;
    HANDLE h[8];
    for( k = 0; k < m_nThreads; ++k)
        h[k] = m_therads[k]->m_evReady;

    WaitForMultipleObjects(m_nThreads,h,true,INFINITE);

    for( k = 0; k < m_nThreads; ++k)
    {
        if(m_therads[k])
            m_therads[k]->m_evReady.Reset();
    }
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::SetSummCount(int cnt, int *pN)
{
    CMTSingleLock   lock(m_locks + LVI_ALL, true);
    bool bClear = false;

    int i;
    if(m_summ.size() != cnt)
    {
        m_stat.resize(cnt);
        m_summ.resize(cnt);
        m_aura6A.resize(cnt);
        m_aura6B.resize(cnt);

        bClear = true;
    }

    for(i = 0; i < cnt; ++i)
    {
        m_summ[i].id = pN[i];
        m_summ[i].i.clear();;
        m_summ[i].si.clear();;

        m_aura6A[i].m_pBase = this;
        m_aura6B[i].m_pBase = this;

        if(bClear)
            ClearStat(m_stat[i]);
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetSummCount(int *pN)
{
    UINT cnt = m_summ.size();
    if(pN)
    {
        for(UINT i = 0; i  < cnt; ++i)
            pN[i] = m_cfg.GetI1(m_summ[i].id);
    }
    return cnt;
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetSummCount(int pos)
{
    int cnt = m_summ.size();
    if(pos < 0 || pos >= cnt)
        return -1;
    return m_cfg.GetI1(m_summ[pos].id);
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::SetResultPtr(int id, void* ptr, int w, int h)
{
    if( w == m_resultSize.cx && h == m_resultSize.cy )
    {
        std::map<int,DWORD*>::iterator i = m_resultPtr.find(id);
        if(i != m_resultPtr.end() && i->second == ptr)
            return true;
    }
    CMTSingleLock   lock(m_locks + LVI_RESULT, true);
    m_resultPtr[id] = (DWORD*)ptr;
    if(ptr)
        m_resultSize.cx = w, m_resultSize.cy = h;

    m_resultVer = 0;

    if(!ptr)
        return false;
    if( w != m_cfg.GetI1(VI_VAR_SIZE) || h != m_cfg.GetI2(VI_VAR_SIZE) )
        return false;
    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::GetResultPtr(int id, void ** ptr, int* pw, int* ph)
{
    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);

    if(ptr)
        *ptr = m_resultPtr[id];
    if(pw)
        *pw = m_resultSize.cx;
    if(ph)
        *ph = m_resultSize.cy;

    if(!m_resultPtr[id])
        return false;
    if( m_resultSize.cx != w || m_resultSize.cy != h )
        return false;
    return true;
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::Make(int command)
{
    m_cMake[command] ++;
    int cnt = 0;
    for(int k = 0; k < m_nThreads; ++k)
    {
        if(m_therads[k])
        {
            m_therads[k]->m_events[command].Set();
            ++cnt;
        }
    }

    if(cnt)
        WaitThreads();
    m_cMake[command] --;
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetOptimalThreadCount(void)
{
    int nProc = 1;

    if(m_nThreadsRqst > 0)
    {
        nProc = m_nThreadsRqst;
    } else
    {
        SYSTEM_INFO info;
        ZeroMemory(&info,sizeof(info));
        GetSystemInfo(&info);

        nProc = info.dwNumberOfProcessors;
    }
    if( nProc > 8)
        return 8;
    if(nProc > 1)
        return nProc;

    return 1;

}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeStatSum(void)
{
    int t;
    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);
    int wh = w*h;
    int cnt = m_stat.size();
    float fw = (float)w;
    float fh = (float)h;
    float fwh = (float)wh;

    if(!wh)
        return;

    for(int i = 0 ; i < cnt; ++i)
    {
        BOOL disableSum = m_cfg.GetI1(VI_FILTER_DISABLE_VI0+i);

        SUMM_STAT& S = m_stat[i];

        if(disableSum)
        {
            if(!S.bClear)
                ClearStat(S);
            continue;
        }

        S.bClear = false;

        for(t = 0 ; t < m_nThreads; ++t)
        {
            CVIEngineThread *pt = m_therads[t];

            S.dsumAi += pt->m_stat[i].dsumA;
            S.dsumBi += pt->m_stat[i].dsumB;

            S.sumAi += pt->m_stat[i].sumAi;
            S.sumBi += pt->m_stat[i].sumBi;

            S.cntAi += pt->m_stat[i].cntAi;
            S.cntBi += pt->m_stat[i].cntBi;
        }


        S.dsumAin = S.dsumAi / fwh;
        S.dsumBin = S.dsumBi / fwh;

        S.sumAin = S.sumAi / fwh;
        S.sumBin = S.sumBi / fwh;

        S.cntAin = S.cntAi / fwh;
        S.cntBin = S.cntBi / fwh;
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::ClearStat(SUMM_STAT& S)
{
    S.sumAi = 0;
    S.sumBi = 0;
    S.sumAin = 0;
    S.sumBin = 0;

    S.cntAi = 0;
    S.cntBi = 0;
    S.cntAin = 0;
    S.cntBin = 0;

    S.dsumAi = 0;
    S.dsumBi = 0;
    S.dsumAin = 0;
    S.dsumBin = 0;

    S.auraA.imgCenterX = 0;

    S.auraA.outlineL.set32(-1);
    S.auraA.outlineR.set32(-1);

    S.auraB.imgCenterX = 0;
    S.auraB.outlineL.set32(-1);
    S.auraB.outlineR.set32(-1);

    S.auraA.line.set0();
    S.auraB.line.set0();

    S.auraA.statCM = 0;
    S.auraA.statCD = 0;
    S.auraA.statCS = 0;
    S.auraA.statCnt = 0;
    S.auraA.statSim = 0;

    S.auraB.statCM = 0;
    S.auraB.statCD = 0;
    S.auraB.statCS = 0;
    S.auraB.statCnt = 0;
    S.auraB.statSim = 0;

    S.auraA.statHist.set0();
    S.auraA.statHistA.set0();
    S.auraA.statHistW.set0();
    S.auraA.statHistC.set0();

    S.auraB.statHist.set0();
    S.auraB.statHistA.set0();
    S.auraB.statHistW.set0();
    S.auraB.statHistC.set0();

    ZeroMemory(&S.auraA.sline,sizeof(S.auraA.sline));
    ZeroMemory(&S.auraB.sline,sizeof(S.auraB.sline));

    S.bClear = true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::ClearStat(void)
{
    int cnt = m_stat.size();

    for(int i = 0 ; i < cnt; ++i)
    {
        SUMM_STAT& S = m_stat[i];
        ClearStat(S);
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::SetRegistry(LPCTSTR group)
{
    m_cfg.SetRegistry(group);
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::GetSrcLine8(int x, int y, float* px, float* py)
{
    if( x < 0 || x >= m_imgSrc8.w || y < 0 || y >= m_imgSrc8.h )
        return false;

    if(px)
    {
        BYTE *pa = m_imgSrc8[y];
        BYTE *pe = pa + m_imgSrc8.w;
        while(pa != pe)
        {
            *px = *pa;
            ++pa,++px;
        }
    }
    if(py)
    {
        BYTE *pa = m_imgSrc8[0]+x;
        BYTE *pe = m_imgSrc8[m_imgSrc8.h-1]+x;;
        while(pa != pe)
        {
            *py = *pa;
            pa += m_imgSrc8.w,++py;
        }
    }
    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::GetSumHist(int id, float* px, float* py)
{
    if(m_bLock)
        return false;

    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);
    if(!w || !h)
        return false;

    if((id & VI_RESULT_SRC_MASK) || (id & VI_RESULT_DELTA_MASK))
        return false;

    UINT n = res2n(id);
    if(n >= m_stat.size())
        return false;

    SUMM_STAT &S = m_stat[n];
    CVIEngineXHist *phx = 0,*phy = 0;

    if( IsModeA(id) )
        phx = & S.xhistAX, phy = & S.xhistAY;
    else
        phx = & S.xhistBX, phy = & S.xhistBY;

    if(phx->m_hist.size() != w || phy->m_hist.size() != h)
        return false;

    if(px)
        phx->m_hist.exportto(px);
    if(py)
        phy->m_hist.exportto(py);

    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::GetSrcLine(int x, int y, float* px, float* py)
{
    if(m_bLock)
        return GetSrcLine8(x,y,px,py);

    if(m_arrSrc.empty())
        return false;
    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);

    if(y < 0 || y >= h || x < 0 || x >= w)
        return false;

    ilFRAME_IMG iSrc = m_arrSrc.begin();

    float *pImg = iSrc->i.begin();

    int mode = m_cfg.GetI1(VI_MODE_RESULT);
    bool bMul255 = false;

    SUM_IMG *pSum = 0;
    FRAME_IMG *pDelta = 0;
    switch( mode )
    {
        default:
            return false;
        case VI_RESULT_SRC_0:
        case VI_RESULT_SRC_A:
        case VI_RESULT_SRC_B:
            break;
        case VI_RESULT_VI0_A:
            pSum = &m_summ[0];
            if(pSum->i.empty())
                return false;
            pImg = pSum->i[h*2];
            break;
        case VI_RESULT_VI0_B:
            pSum = &m_summ[0];
            if(pSum->i.empty())
                return false;
            pImg = pSum->i[h*3];
            break;
        case VI_RESULT_VI1_A:
            pSum = &m_summ[1];
            if(pSum->i.empty())
                return false;
            pImg = pSum->i[h*2];
            break;
        case VI_RESULT_VI1_B:
            pSum = &m_summ[1];
            if(pSum->i.empty())
                return false;
            pImg = pSum->i[h*3];
            break;
        case VI_RESULT_VI2_A:
            pSum = &m_summ[2];
            if(pSum->i.empty())
                return false;
            pImg = pSum->i[h*2];
            break;
        case VI_RESULT_VI2_B:
            pSum = &m_summ[2];
            if(pSum->i.empty())
                return false;
            pImg = pSum->i[h*3];
            break;
        case VI_RESULT_DELTA_A:
        {
            ilFRAME_IMG iDelta = m_arrDelta.begin();
            pDelta = &(*iDelta);
            if(pDelta->i.empty())
                return false;
            pImg = pDelta->i[0];
        }
        break;
        case VI_RESULT_DELTA_B:
        {
            ilFRAME_IMG iDelta = m_arrDelta.begin();
            pDelta = &(*iDelta);
            if(pDelta->i.empty())
                return false;
            pImg = pDelta->i[0],bMul255 = true;
        }
        break;
    }


    if(pImg)
    {
        if(px)
        {
            float *p = pImg +y*w;
            if(bMul255)
            {
                for( int i = 0; i < h; ++i )
                    px[i] = (p[i]!=0)?255.0f:0;
            } else
                SSESafeMemcpy(px,p,w*sizeof(float));
        }

        if(py)
        {
            if(bMul255)
            {
                for( int i = 0; i < h; ++i )
                    py[i] = (pImg[i*w +x]!=0)?255.0f:0;
            }else
            {
                for( int i = 0; i < h; ++i )
                    py[i] = pImg[i*w +x];
            }
        }
    }

    return true;
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::StatUpdate(void)
{
    SUMM_STAT *ps = 0;

    ps = &m_stat[0];
    m_cfg.PutF1( VI_VAR_STAT_INTEGR0A, ps->sumAin );
    m_cfg.PutF1( VI_VAR_STAT_INTEGR0B, ps->sumBin );

    ps = &m_stat[1];
    m_cfg.PutF1( VI_VAR_STAT_INTEGR1A, ps->sumAin );
    m_cfg.PutF1( VI_VAR_STAT_INTEGR1B, ps->sumBin );

    ps = &m_stat[2];
    m_cfg.PutF1( VI_VAR_STAT_INTEGR2A, ps->sumAin );
    m_cfg.PutF1( VI_VAR_STAT_INTEGR2B, ps->sumBin );

    ps = &m_stat[0];

    BOOL disabled2X =m_cfg.GetI1(VI_FILTER_DISABLE_2X);

    ///////////////////////////////
    // A1
    ///////////////////////////////
    m_cfg.PutF1( VI_VAR_STAT_RES_A1, ps->dsumAin );
    if(disabled2X)
        m_cfg.PutF1( VI_VAR_STAT_RES_A1X, ps->dsumAin );

    ///////////////////////////////
    // A2
    ///////////////////////////////
    {
        ps = &m_stat[1];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        float   sl = ps->auraA.sline.sumL;
        float   sr = ps->auraA.sline.sumR;
        float sum = 0;
        if( cl )
            sum += sl/cl;
        if( cr )
            sum += sr/cr;

        m_cfg.PutF1( VI_VAR_STAT_RES_A2,sum/2.0f );
    }
    ///////////////////////////////

    ///////////////////////////////
    // A3
    ///////////////////////////////
    {
        ps = &m_stat[0];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        float   sl = ps->auraA.sline.sumL;
        float   sr = ps->auraA.sline.sumR;
        float sum = 0;
        if( cl )
            sum += sl/cl;
        if( cr )
            sum += sr/cr;

        m_cfg.PutF1( VI_VAR_STAT_RES_A3,sum/2.0f );
    }
    ///////////////////////////////

    ///////////////////////////////
    // A4
    ///////////////////////////////
    m_cfg.PutF1( VI_VAR_STAT_RES_A4, m_statAVG.Get(VI_VAR_STAT_RES_A1) );
    if(disabled2X)
        m_cfg.PutF1(VI_VAR_STAT_RES_A4X, m_statAVG.Get(VI_VAR_STAT_RES_A1X) );

    ///////////////////////////////
    // F1
    ///////////////////////////////
    m_cfg.PutF1( VI_VAR_STAT_RES_F1, ps->dsumBin );
    if(disabled2X)
    {
        m_cfg.PutF1( VI_VAR_STAT_RES_F1X, ps->dsumBin );
        // F6
        m_procF6.add( m_cfg.GetF1(VI_VAR_STAT_RES_F1X) ) ;
    }
    ///////////////////////////////
    // F2
    ///////////////////////////////
    {
        ps = &m_stat[1];
        int     cl = ps->auraB.sline.cl;
        int     cr = ps->auraB.sline.cr;
        float   sl = ps->auraB.sline.sumL;
        float   sr = ps->auraB.sline.sumR;
        float sum = 0;
        if( cl )
            sum += sl/cl;
        if( cr )
            sum += sr/cr;

        m_cfg.PutF1( VI_VAR_STAT_RES_F2,sum/2.0f );
    }
    ///////////////////////////////

    ///////////////////////////////
    // F3
    ///////////////////////////////
    {
        ps = &m_stat[0];
        int     cl = ps->auraB.sline.cl;
        int     cr = ps->auraB.sline.cr;
        float   sl = ps->auraB.sline.sumL;
        float   sr = ps->auraB.sline.sumR;
        float sum = 0;
        if( cl )
            sum += sl/cl;
        if( cr )
            sum += sr/cr;

        m_cfg.PutF1( VI_VAR_STAT_RES_F3,sum/2.0f );
    }
    ///////////////////////////////

    ///////////////////////////////
    // F4
    ///////////////////////////////
    {
        ps = &m_stat[0];
        m_cfg.PutF1( VI_VAR_STAT_RES_F4, max(ps->auraA.sline.maxL,ps->auraA.sline.maxR)  );
    }
    ///////////////////////////////

    ///////////////////////////////
    // F5
    ///////////////////////////////
    if(!IsSkip())
    {
        m_cfg.PutF1( VI_VAR_STAT_RES_F5, m_statFFT.GetHfLf(VI_VAR_STAT_RES_F1) );

        if(disabled2X)
            m_cfg.PutF1(VI_VAR_STAT_RES_F5X, m_statFFT.GetHfLf(VI_VAR_STAT_RES_F1X) );
    } else
    {
        m_cfg.PutF1( VI_VAR_STAT_RES_F5, 0 );

        if(disabled2X)
            m_cfg.PutF1(VI_VAR_STAT_RES_F5X, 0 );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S1
    ///////////////////////////////
    {
        ps = &m_stat[2];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        float   sl = ps->auraA.sline.sumL;
        float   sr = ps->auraA.sline.sumR;
        int crl = cl+cr;
        float sum = 0;
        if(crl)
            sum = (sl-sr)/crl;

        m_cfg.PutF1( VI_VAR_STAT_RES_S1,sum );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S2
    ///////////////////////////////
    {
        ps = &m_stat[1];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        float   sl = ps->auraA.sline.sumL;
        float   sr = ps->auraA.sline.sumR;
        int crl = cl+cr;
        float sum = 0;
        if(crl)
            sum = (sl-sr)/crl;

        m_cfg.PutF1( VI_VAR_STAT_RES_S2,sum );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S3
    ///////////////////////////////
    {
        ps = &m_stat[0];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        float   sl = ps->auraA.sline.sumL;
        float   sr = ps->auraA.sline.sumR;
        int crl = cl+cr;
        float sum = 0;
        if(crl)
            sum = (sl-sr)/crl;

        m_cfg.PutF1( VI_VAR_STAT_RES_S3,sum );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S4
    ///////////////////////////////
    {
        ps = &m_stat[2];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        int crl = cl+cr;
        float sum = 0;
        if(crl)
            sum = (float)(cl-cr)/(float)crl;

        m_cfg.PutF1( VI_VAR_STAT_RES_S4,sum );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S5
    ///////////////////////////////
    {
        ps = &m_stat[1];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        int crl = cl+cr;
        float sum = 0;
        if(crl)
            sum = (float)(cl-cr)/(float)crl;

        m_cfg.PutF1( VI_VAR_STAT_RES_S5,sum );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S6
    ///////////////////////////////
    {
        ps = &m_stat[0];
        int     cl = ps->auraA.sline.cl;
        int     cr = ps->auraA.sline.cr;
        int crl = cl+cr;
        float sum = 0;
        if(crl)
            sum = (float)(cl-cr)/(float)crl;

        m_cfg.PutF1( VI_VAR_STAT_RES_S6,sum );
    }
    ///////////////////////////////

    ///////////////////////////////
    // S7
    ///////////////////////////////
    {
        ps = &m_stat[0];
        m_cfg.PutF1( VI_VAR_STAT_RES_S7, ps->auraA.sline.maxL-ps->auraA.sline.maxR );
    }
    ///////////////////////////////

    ///////////////////////////////
    // P1
    ///////////////////////////////
    {
        ps = &m_stat[1];
        m_cfg.PutF1( VI_VAR_STAT_RES_P1, ps->auraB.statCS );
    }
    ///////////////////////////////

    ///////////////////////////////
    // P2
    ///////////////////////////////
    {
        ps = &m_stat[0];
        m_cfg.PutF1( VI_VAR_STAT_RES_P2, ps->auraB.statCS );
    }
    ///////////////////////////////

    ///////////////////////////////
    // P3
    ///////////////////////////////
    {
        ps = &m_stat[1];
        m_cfg.PutF1( VI_VAR_STAT_RES_P3, ps->auraB.statSim );
    }
    ///////////////////////////////

    ///////////////////////////////
    // P4
    ///////////////////////////////
    {
        ps = &m_stat[0];
        m_cfg.PutF1( VI_VAR_STAT_RES_P4, ps->auraB.statSim );
    }
    ///////////////////////////////


    ///////////////////////////////
    // P8A
    ///////////////////////////////
    {
        ps = &m_stat[0];
        mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
        m_cfg.PutF1( VI_VAR_STAT_RES_P8A, MakeCharming(hist.p,hist.s));
    }
    ///////////////////////////////

    ///////////////////////////////
    // P8F
    ///////////////////////////////
    {
        ps = &m_stat[0];
        mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
        m_cfg.PutF1( VI_VAR_STAT_RES_P8F, MakeCharming(hist.p,hist.s));
    }
    ///////////////////////////////


    if(! m_cfg.GetI1(VI_FILTER_DISABLE_ENTR) )
    {
        ///////////////////////////////
        // P9A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P9A, MakeEntropyH(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P9F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P9F, MakeEntropyH(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P10A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P10A, MakeEntropyD(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P10F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P10F, MakeEntropyD(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P11A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P11A, MakeEntropyX(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P11F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P11F, MakeEntropyX(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P12A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P12A, MakeEntropyS(hist.p,hist.s));
        }
        ///////////////////////////////

        ///////////////////////////////
        // P12F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
            m_cfg.PutF1( VI_VAR_STAT_RES_P12F, MakeEntropyS(hist.p,hist.s));
        }
        ///////////////////////////////
    }

    ///////////////////////////////
    // P16
    ///////////////////////////////
    {
        ps = &m_stat[0];
        mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;

        float CN = MakeComN(hist.p,hist.s);
        float CS = MakeComS(m_stat[0].auraA);

        m_cfg.PutF1( VI_VAR_STAT_RES_P16, CN);
        m_cfg.PutF1( VI_VAR_STAT_RES_P17, CS);
        m_cfg.PutF1( VI_VAR_STAT_RES_P18, (CN+CS)*0.5f );
    }
    ///////////////////////////////
}
/// <summary>
/// </summary>
/// <param name=""></param>
DWORD CVIEngineBase::AddImageThread8(LPVOID lpParameter)
{
    CVIEngineBase *pThis = (CVIEngineBase *)lpParameter;
    pThis->AddImageThreadLocal8();
    CloseHandle(pThis->m_hThreadAddImage8);
    pThis->m_hThreadAddImage8 = 0;
    return 0;
}

/// <summary>
/// </summary>
/// <param name=""></param>
DWORD CVIEngineBase::AddImageThread(LPVOID lpParameter)
{
    CVIEngineBase *pThis = (CVIEngineBase *)lpParameter;
    pThis->AddImageThreadLocal();
    CloseHandle(pThis->m_hThreadAddImage);
    pThis->m_hThreadAddImage = 0;
    return 0;
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::AddImageThreadProc()
{
    CMTSingleLock lock(m_locks + LVI_SRC, false);

    DWORD tS,tE,tD; // Время начала работы данного процесса, время его завершения работы, продолжительность его работы
    std::list< SRC_IMG > cur;

    float fpsMax = m_cfg.GetF1(VI_VAR_FPSMAXR); // Максимальное количество обрабатываемых кадров/сек из настроек алгоритмов программы

	// Время начала работы данного процесса
	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	tS = GetTickCount();

    if( m_cfg.GetI1(VI_FILTER_PAUSE) )
    {
        return 10;
    }

    lock.Lock();

    BOOL bFpsDiv = (m_cfg.GetI1(VI_FILTER_FPSDIV));
    if( m_srcF.empty() )
        return 1;

    cur.splice( cur.end(), m_srcF, m_srcF.begin() );

    ///////////////////////////////////////////////////////
    // делитель чк
    ///////////////////////////////////////////////////////
    if(bFpsDiv)
    {
        if(m_srcF.size() > 0)
            m_srcF.clear();

        int div = 1;
        if(m_cfg.GetI1(VI_FILTER_FPS2IN))
        {
            m_divMaker.clear();
            div = m_cfg.GetI1(VI_FILTER_FPS2IN);
        }
        else
            if(fpsMax > 0 && m_cfg.GetI1(VI_VAR_NFRAME_IN) > 2)
            {
                float fpsIn = m_cfg.GetF1(VI_VAR_FPSIN);
                div = round(fpsIn/fpsMax);
                if(div)
                    m_divMaker.push_back(div);
                div = Median(m_divMaker,9);
            }

            bool bSkip = false;

        if(m_cfg.GetI1(VI_VAR_NFRAME_IN) < 5)
            bSkip = true;
        else
            if(div > 1)
            {
                int N = m_cfg.GetI1(VI_VAR_NFRAME_IN);
                if(N%div != 0 || N < 5)
                    bSkip = true;
            }

            if(bSkip)
                return 1;
    }
    ///////////////////////////////////////////////////////

    m_cfg.PutI1(VI_VAR_FPS_BUFFER_SIZE, m_srcF.size());
    lock.Unlock();

    bool bMake = true;

    if(bMake && !m_bDone)
    {
        SRC_IMG& img = cur.front();
        MakeImage(img.i.begin(),img.ib.begin(),img.i.w,img.i.h);
    }
    cur.clear();

	// Время завершения работы данного процесса
	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	tE = GetTickCount();
	// Вычисление времени работы данного процесса (в тиках) 1000 тиков = 1 сек
	tD = tE - tS;

    if(!bFpsDiv && fpsMax > 0)
    {
        DWORD tR = (int)(1000.0f/fpsMax);
        if( tD < tR )
        {
            DWORD dt = tR - tD;
            if(dt > 2000) dt = 2000;
            return dt;
        }
    }
    return 0;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::AddImageThreadLocal()
{
    while(!m_bDone && !m_bStop)
    {
        if(!m_cfg.GetI1(VI_FILTER_FPSDIV))
        {
            UINT dt = AddImageThreadProc();
            if(dt) Sleep(min(dt,2000));
        }
        else
            Sleep(250);

    }

    Sleep(250);

    CMTSingleLock lock(m_locks + LVI_SRC, false);
    lock.Lock();
    m_srcF.clear();
    lock.Unlock();
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::AddImageThreadLocal8()
{
    while(!m_bDone && !m_bStop)
    {
        int ev = m_events[EVI_ADD8].Wait(INFINITE,m_events[EVI_DONE].m_hEvent);
        switch(ev)
        {
            case 1:
                m_events[EVI_ADD8_READY].Set();
                return;
            case 0:
                AddImage8();
                m_events[EVI_ADD8_READY].Set();
                break;
        }

    }
    m_events[EVI_ADD8_READY].Set();
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::Stop(void)
{
    m_bStop = 1;
    m_cfg.RegSave();

    m_audio.m_bDone = true;

    Sleep(500);

    CloseThreads();

    while( m_audio.m_hThread )
        Sleep(5);

    #ifndef SEQ_DISABLE_FACE
    SAFE_DELETE(m_pFace);
    #endif
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::Pause(bool bSet)
{
    m_cfg.PutI1(VI_FILTER_PAUSE,bSet?1:0);
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::Reset(bool bReset)
{
    if(!bReset && m_cfg.GetI1(VI_VAR_SIZE))
    {
        m_cfg.PutI1(VI_VAR_RESET,1);
        return;
    }

    Pause(true);

    ClearStat();
    int nSum = m_summ.size(),k;

    #ifndef SEQ_DISABLE_LD
    m_statLDF.Reset();
    #endif

    for( k = 0; k < nSum; ++k )
    {
        m_summ[k].i.set0();
        m_summ[k].si.set0();
        m_stat[k].auraA.line.set0();
        m_stat[k].auraB.line.set0();

        m_stat[k].auraA.outlineL.set0();
        m_stat[k].auraB.outlineR.set0();

        m_srcMask.set0();
        m_fpsIn.Reset();
        m_fpsOutF.Reset();
        m_fpsDropF.Reset();
        m_fpsOutR.Reset();
        m_fpsDropR.Reset();
    }

    ilFRAME_IMG i;

    for(i = m_arrDelta.begin(); i != m_arrDelta.end(); ++i)
        i->i.set0();

    for( k = VI_STAT_START; k < VI_STAT_END; ++k )
        m_cfg.PutF1(k,0);

    for( k = VI_STAT_EXT_START; k < VI_STAT_EXT_END; ++k )
        m_cfg.PutF1(k,0);

    m_statFFT.Reset();
    m_statAVG.Reset();
    m_procF6.Reset();

    m_cfg.PutI1(VI_VAR_NDROP,0);

    Pause(false);

    m_cfg.PutI1(VI_VAR_RESET,0);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeStatFS2(float* src, int sw, int sh)
{
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::FlushFPS(void)
{
    float fpsIn = m_fpsIn.Get();
    float fpsOutF = m_fpsOutF.Get();
    float fpsOutR = m_fpsOutR.Get();
    m_cfg.PutF1(VI_VAR_FPSIN,   fpsIn );
    m_cfg.PutF1(VI_VAR_FPSOUTF, min(fpsIn,fpsOutF) );
    m_cfg.PutF1(VI_VAR_FPSOUTR, min(fpsIn,fpsOutR) );
    m_cfg.PutF1(VI_VAR_FPSDROPF,    m_fpsDropF.Get() );
    m_cfg.PutF1(VI_VAR_FPSDROPR,    m_fpsDropR.Get() );
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::Start(void)
{
    m_cfg.Init();
    #ifndef SEQ_DISABLE_LD
    m_statLDF.Init();
    #endif

    int id[3] = { VI_VAR_N0, VI_VAR_N1, VI_VAR_N2 };
    SetSummCount(3,id);
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::CheckNRqst(int w, int h)
{
    bool bNew = false;
    int cw,ch;
    m_cfg.GetI(VI_VAR_SIZE,cw,ch);

    if(cw != w) bNew = true;
    if(ch != h) bNew = true;

    int n0c,n0r;
    int n1c,n1r;
    int n2c,n2r;

    n0c = m_cfg.GetI1(VI_VAR_N0);
    n1c = m_cfg.GetI1(VI_VAR_N1);
    n2c = m_cfg.GetI1(VI_VAR_N2);

    n0r = m_cfg.GetI1(VI_VAR_N0_RQST);
    n1r = m_cfg.GetI1(VI_VAR_N1_RQST);
    n2r = m_cfg.GetI1(VI_VAR_N2_RQST);

    if( n0c != n0r ) bNew = true;
    if( n1c != n1r ) bNew = true;
    if( n2c != n2r ) bNew = true;


    if( !bNew )
        return false;

    Reset(true);

    m_cfg.PutI1(VI_VAR_N0,n0r);
    m_cfg.PutI1(VI_VAR_N1,n1r);
    m_cfg.PutI1(VI_VAR_N2,n2r);

    int mCnt = max( n0r, max(n1r,n2r) );
    if(mCnt < 2)
        mCnt = 2;
    SetSize(w,h,mCnt);

    return true;
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeResultSrc()
{
    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);

    CVIEngineThread proc(this);
    proc.m_pCfg = &m_cfg;
    proc.m_yS = 0;
    proc.m_yE = h;
    proc.m_nId = 0;

    if(m_resultPtr[VI_RESULT_SRC_0] )
    {
        proc.MakeResultSrc(VI_RESULT_SRC_0);
        m_vPos.MakeDraw( m_resultPtr[VI_RESULT_SRC_0] );
    }
    if(m_resultPtr[VI_RESULT_SRC_A] )
    {
        proc.MakeResultSrc(VI_RESULT_SRC_A);
    }
    if(m_resultPtr[VI_RESULT_SRC_B] )
    {
        proc.MakeResultSrc(VI_RESULT_SRC_B);
    }

    MakeFaceDraw();

    ++m_resultVer;
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetStatHistN(int res, int* pHist256, float *pFPS)
{
    int nSum = res2n(res);

    if(m_statRelease.empty())
        return false;

    AURA_STAT & stat = IsModeA(res) ? m_statRelease[nSum].auraA : m_statRelease[nSum].auraB;
    mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? stat.statHistW : stat.statHist;

    if(hist.empty())
        return 0;
    if(pHist256)
        hist.exportto(pHist256);
    if(pFPS) *pFPS = m_cfg.GetF1( VI_VAR_FPSOUTR );
    return m_cfg.GetI1(VI_VAR_NFRAME);
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetStatHistC(int res, int* pHist256, float *pFPS)
{
    int nSum = res2n(res);

    if(m_statRelease.empty())
        return false;
    AURA_STAT & stat = IsModeA(res) ? m_statRelease[nSum].auraA : m_statRelease[nSum].auraB;
    mmx_array<int>& hist = stat.statHistA;
    if(hist.empty())
        return 0;
    if(pHist256)
        hist.exportto(pHist256);
    if(pFPS) *pFPS = m_cfg.GetF1( VI_VAR_FPSOUTR );
    return m_cfg.GetI1(VI_VAR_NFRAME);
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetStatHistF(int res, int* pHist256, float *pFPS)
{
    int nSum = res2n(res);
    if(m_statRelease.empty())
        return false;
    AURA_STAT & stat = IsModeA(res) ? m_statRelease[nSum].auraA : m_statRelease[nSum].auraB;
    mmx_array<int>& hist = stat.statHistC;
    if(hist.empty())
        return 0;
    if(pHist256)
        hist.exportto(pHist256);
    if(pFPS) *pFPS = m_cfg.GetF1( VI_VAR_FPSOUTR );
    return m_cfg.GetI1(VI_VAR_NFRAME);
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::GetStatHistFT(int res, int* pHist256, float *pDT)
{
    if(pDT) *pDT = m_cfg.GetF1( 1.0f/VI_VAR_FPSOUTF );
    if(res == VI_VAR_HIST_F6)
        return m_procF6.GetHist(pHist256,pDT,m_procF6.m_bufRes);
    if(res == VI_VAR_HIST_F8)
        return m_procF6.GetHist(pHist256,pDT,m_procF6.m_bufSrc);
    return 0;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeAnger(void)
{
    MakeAnger(true);
    MakeAnger(false);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeAnger(bool bModeB)
{
    mmx_array<int>& hist = bModeB ? m_stat[0].auraB.statHistW : m_stat[0].auraA.statHistW;

    if(hist.empty())
        return;

    int Fm = 0,Hm = 0, h;
    int M, sumF = 0, sumH = 0, cnt = 0, sumD = 0;
    int x;

    // максимум и среднее
    for(x=1; x<256; ++x)
    {
        h = hist[x];
        if(!h)
            continue;
        sumH += h;
        sumF += h*x;
        ++cnt;
        if(h > Hm)
            Hm = h, Fm = x;
    }
    if(!cnt)
        return;

    M = (sumF+(sumH>>1))/sumH;

    // дисперсия
    for(x=1; x<256; ++x)
    {
        h = hist[x];
        if(!h)
            continue;
        int df = M-x;
        sumD += df*df*h;
    }

    float D = ((float)sumD) / ((float)sumH);
    float S = sqrtf( D );

    float anger = 0;
    if(!IsSkip())
        anger = (Fm + 4.0f*S)/512.0f;

    if(bModeB)
        m_cfg.PutF1(VI_VAR_STAT_RES_P7F,anger);
    else
        m_cfg.PutF1(VI_VAR_STAT_RES_P7A,anger);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeStress(void)
{
    MakeStress(true);
    MakeStress(false);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeStress(bool bModeB)
{
    int w,h,y;
    m_cfg.GetI(VI_VAR_SIZE,w,h);
    AURA_STAT& S = bModeB ? m_stat[0].auraB : m_stat[0].auraA;

    if(S.line.empty())
        return;

    float sum = 0;
    int sc = 0;

    for( y = 0; y < h; ++y)
    {
        AURA_STAT_LINE& l = S.line[y];
        int fl = l.aColorL;
        int fr = l.aColorR;
        if(fl <= 0 || fr <= 0) continue;

        int wl = l.cwl;
        int wr = l.cwr;
        if(wl <= 0 || wr <= 0) continue;

        float dc = fabs( ((float)(fl - fr))/(float)max(fl,fr));
        float ds = fabs( ((float)(wl - wr))/(float)max(wl,wr));

        sum += (dc+ds);
        ++sc;
    }

    float stress = 0;
    if(sc > 0 && !IsSkip() )
    {
        stress = sum / (2*sc);
        if(stress > 1) stress = 1;
    }

    if(bModeB)
        m_cfg.PutF1(VI_VAR_STAT_RES_P6F,stress);
    else
        m_cfg.PutF1(VI_VAR_STAT_RES_P6A,stress);
}

/// <summary>
/// Чтение настроечного параметра VI_VAR_STAT_CFG_SIN
/// Вычисление sin ( t * 2 * M_PI * n )
/// где t - значение текущего времени по таймеру m_timer
/// M_PI = 3.1415926...
/// n - значение настроечного параметра VI_VAR_STAT_CFG_SIN
/// И сохранение вычисленного значения в настроечном параметре VI_VAR_STAT_RES_SIN
/// То есть 
/// настроечный параметр VI_VAR_STAT_CFG_SIN является частотой синусоидального сигнала
/// настроечный параметр VI_VAR_STAT_RES_SIN хранит значение синусоидального сигнала в текущий момент времени по таймеру m_timer
/// </summary>
void CVIEngineBase::MakeSin(void)
{
    double t = m_timer.Get();
    double n = m_cfg.GetF1(VI_VAR_STAT_CFG_SIN);
    double v = sin( t*2*M_PI*n );
    m_cfg.PutF1(VI_VAR_STAT_RES_SIN,(float)v);
}


/// <summary>
/// Вызов одноимённой функции с параметром из списка парамертов по-очереди
///         VI_RESULT_VI2_A,
///         VI_RESULT_VI2_B,
///         VI_RESULT_VI1_A,
///         VI_RESULT_VI1_B,
///         VI_RESULT_VI0_A,
///         VI_RESULT_VI0_B
/// </summary>
void CVIEngineBase::tmp_aura_draw(void)
{
    int id[] =
    {
        VI_RESULT_VI2_A,
        VI_RESULT_VI2_B,
        VI_RESULT_VI1_A,
        VI_RESULT_VI1_B,
        VI_RESULT_VI0_A,
        VI_RESULT_VI0_B
    };

    for(int k = 0; k < sizeof(id)/sizeof(id[0]); ++k)
        tmp_aura_draw(id[k]);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::tmp_aura_draw(int res)
{
    int n = res2n(res);
    bool bModeA = IsModeA(res);
    CVIEngineAura6 &aura6 = bModeA ? m_aura6A[n] : m_aura6B[n];

    int w,h;
    void *pi;
    if(! GetResultPtr(res,&pi,&w,&h) )
        return;

    aura6.MakeExportSum((DWORD*)pi,(COLORREF*)m_palI,w,h,0,0);
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::OnNewVarDidable(int id)
{
    int v = m_cfg.GetI1(id);
    if(!v)
        return;

    Reset();
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineBase::IsMotion(bool bSet)
{
    if(!bSet)
    {
        if(m_cfg.GetI1(VI_FILTER_DISABLE_VI1))
            m_cfg.PutI1(VI_FILTER_MOTION_SET,0);
        return m_cfg.GetI1(VI_FILTER_MOTION_SET);
    }

    if(!m_cfg.GetI1(VI_FILTER_MOTION))
    {
        m_cfg.PutI1(VI_FILTER_MOTION_SET,0);
        return 0;
    }

    float lev = m_cfg.GetF1(VI_FILTER_MOTION_LEVEL);
    float v = m_cfg.GetI1(VI_FILTER_MOTION_10X) ? m_cfg.GetF1(VI_VAR_STAT_INTEGR1A):m_cfg.GetF1(VI_VAR_STAT_INTEGR0A);
    if(v <= lev)
    {
        int set = m_cfg.GetI1(VI_FILTER_MOTION_SET);
        int n = (set > 0) ? set+1 : 1;
        m_cfg.PutI1(VI_FILTER_MOTION_SET,n);
        return n;
    } else
    {
        float lev2 = m_cfg.GetF1(VI_FILTER_MOTION_LEVEL2);
        float fi10 = m_cfg.GetF1(VI_VAR_STAT_INTEGR1A);
        float fiN = m_cfg.GetF1(VI_VAR_STAT_INTEGR0A);

        if( fi10 > lev2 || (fi10 > lev2/2.0f && fi10 > fiN * 5.0f) )
        {
            int set = m_cfg.GetI1(VI_FILTER_MOTION_SET);
            int n = (set < 0) ? set-1 : -1;
            m_cfg.PutI1(VI_FILTER_MOTION_SET,n);
            return n;
        }
    }

    m_cfg.PutI1(VI_FILTER_MOTION_SET,0);
    return 0;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeMotion(void)
{
    int noise = IsMotion(true);
    bool bReset = false;
    if(m_cfg.GetI1(VI_FILTER_MOTION_AUTO_RESET))
    {
        if(m_statRelease.size() >= 2 && m_statRelease[1].cntAin > 0.7f)
            bReset = true;
        else
            if(noise == 3 || noise == -2 )
                bReset = true;
    }
    if(bReset)
    {
        Reset();
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeStateMacro(void)
{
    #ifndef SEQ_DISABLE_MACRO_MODE_PLUS
    #ifndef SEQ_DISABLE_FACE
    if( m_cfg.GetI1(VI_MACRO_FACE) )
    {
        if( m_cfg.GetI1(VI_FACE_ENABLE)&& m_pFace)
            return m_pFace->MakeStatRelease();
        return 0;
    }
    #endif
    #endif
    if(m_statRelease.empty())
        return 0;
    SUMM_STAT& stat = m_statRelease[0];
    mmx_array<int>& hist = stat.auraB.statHistA;
    if(hist.size() != 256)
        return 0;

    int w,h,i,wh,lL;
    double lS;

    m_cfg.GetI(VI_VAR_SIZE,w,h);
    wh = w*h;
    lL = m_cfg.GetI1(VI_MACRO_LEVEL_L);
    lS = m_cfg.GetF1(VI_MACRO_LEVEL_S)/100.0;

    UINT Nn = (int)(lS*wh + 0.5f),N = 0;
    UINT Fs = 0;

    for( i = lL; i < 256; ++i )
    {
        int h = hist[i];
        if(!h) continue;

        N += h;
        Fs += h*i;
    }


    if(N) Fs /= N; else Fs = 0;


    double fF = ((int)Fs>lL) ? (double)(Fs-lL)/((255.0-lL)) : 0;
    double fNr = (N > Nn && N) ? (N-Nn)/(double)N : 0;
    double fNs = 100.0 * N / (double)wh;

    m_cfg.PutF1(VI_VAR_STAT_RES_P13,(float)fF);
    m_cfg.PutF1(VI_VAR_STAT_RES_P14,(float)fNr);
    m_cfg.PutF1(VI_VAR_STAT_RES_P15,(float)fNs);

    if(N < Nn || !N || lL >= 255) // малая площадь
        return 0;

    return (float)( (fF + fNr) / 2 );
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeState(void)
{
    float state0 = 0;

    CMTSingleLock *pFaceLock = 0;

    if(m_cfg.GetI1(VI_MACRO_ENABLE))
    {
        state0 =  MakeStateMacro();
    }
    else
    {

        float Ag = 0;
        float St = 0;
        float Tn = m_cfg.GetF1(VI_VAR_STAT_RES_F5X);

        #ifndef SEQ_LITE
        Ag = m_cfg.GetF1(VI_VAR_STAT_RES_P7);
        St = m_cfg.GetF1(VI_VAR_STAT_RES_P6);
        #else

        Ag = m_cfg.GetF1(VI_VAR_STAT_RES_P7A);
        St = m_cfg.GetF1(VI_VAR_STAT_RES_P6A);
        #endif
        state0 = MakeState(Ag,St,Tn);
        MakeStateMacro();
    }

    if(state0 < 0)
        state0 = 0;

    m_cfg.PutF1(VI_VAR_STATE_VAR_SRC,state0);

    float state = m_statAVG.Get(VI_VAR_STATE_VAR_SRC);
    if(state < 0)
        state = 0;

    float lev = m_cfg.GetF1(VI_VAR_STATE_CRITICAL);

    m_cfg.PutF1(VI_VAR_STATE_CRITICAL,lev);
    m_cfg.PutF1(VI_VAR_STATE_VAR,state);
    m_cfg.PutF1(VI_VAR_STAT_RES_P19,state);

    if(state >= lev)
    {
        m_cfg.PutI1(VI_VAR_STATE_FLAG_A,1);
        m_cfg.PutI1(VI_VAR_STATE_FLAG_P,1);
    } else
        m_cfg.PutI1(VI_VAR_STATE_FLAG_A,0);

    return state;
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeState(float Ag, float St, float Tn)
{
    const float dAg = 0.75f;
    const float dSt = 0.80f;
    const float dTn = 0.60f;

    int ret = 0;
    if(Ag > dAg) ret |= 1;
    if(St > dSt) ret |= 2;
    if(Tn > dTn) ret |= 4;

    switch(ret)
    {
        case 0:
            return (Ag+St+Tn)/3.0f;
        case 1:
        case 2:
        case 4:
            return 0.8f;
        case 3:
        case 5:
        case 6:
        case 7:
            return 1.00f;
        default:
            break;
    }
    return 0;
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeCharming(int* pHist256, int len)
{
    int i;
    double flen = (double)len;
    double M,S,D,CH;

    double fps = m_cfg.GetF1(VI_VAR_FPSOUTR);

    double sV = 0,sVX = 0,sDV = 0;
    for( i = 1 ; i < len; ++i)
    {
        double v = (double)(pHist256[i]);
        double X = i*fps/flen;

        sV  += v;
        sVX += v*X;
    }

    M = (sV == 0)? 0 : sVX/sV;


    for( i = 1 ; i < len; ++i)
    {
        double v = (double)(pHist256[i]);
        double X = i*fps/flen;
        double d = X-M;

        sDV += d*d*v;
    }

    D = (sV == 0)? 0 : sDV/sV;
    S = sqrt(D);

    CH = (fps == 0)? 0 : (M-S)/fps;

    return (float)CH;
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeEntropyH(int* pHist256, int len)
{
    if(!pHist256)
        return 0;
    double m = 0, n = 0, ni;
    double d = 1.0/len;

    int i;

    int i1,i2;
    int *h = pHist256;

    for(i1 = 1; i1 < len; ++i1)
        if(h[i1]) break;
        for(i2 = len-1; i2 >= i1; --i2)
            if(h[i2]) break;

            m = i2-i1+1;

        for( i = i1; i <= i2; ++i )
        {
            ni = h[i];
            n += ni;
        }

        if(n == 0)
            return 0;

        double sum = 0;

        for( i = i1; i <= i2; ++i )
        {
            if(! h[i])
                continue;
            ni = h[i];
            sum +=  (ni/n)*log(n*d/ni);
        }

        return (float)(sum);
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeEntropyD(int* pHist256, int len)
{
    if(!pHist256)
        return 0;
    int m = 0, n = 0, ni, i;
    double d = 1.0/len;
    double Delta,A1;
    double sm = 0;

    int i1,i2;
    int *h = pHist256;

    for(i1 = 1; i1 < len; ++i1)
        if(h[i1]) break;
        for(i2 = len-1; i2 >= i1; --i2)
            if(h[i2]) break;

            m = i2-i1+1;

        for( i = i1; i <= i2; ++i )
        {
            ni = h[i];
            n += ni;
        }
        if(!n)
            return 0;

        for( i = i1; i <= i2; ++i )
        {
            ni = h[i];
            if(ni)
                sm += ni * log10((double)ni);
        }

        Delta = (d*n/2)*pow(10,-1.0*sm/n);

        A1 = 1+(double)m/(2.0*n);

        return (float)(/*A1* */Delta);
}

/// <summary>
/// Вычисление энтропии по гистограмме сигнала (Вариант X - заимствование идеи из статистического критерия X^2)
/// Термин энтропия означает численное обозначение некоторой неопределённости и используется в различных областях науки для анализа данных
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// Сумма pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Затем по этой таблице был вычислено среднее значение M яркости ненулевых элементов (= математическое ожидание без учёта нулевых значений)
/// Для каждого элементв вычисленно отклонение от линейно-равномерного распределения, то есть
/// Если flen - максимальное значение яркости, то для каждого ненулевого значения i вычисляются коэффициенты dd(i) = (i/flen - M)^2
/// И вычисляется значение return ( Сумма dd(i)*pHist256(i) / Сумма pHist256(i) ) / sqrt( Сумма dd(i)*dd(i)*pHist256(i) / Сумма pHist256(i) )
/// </summary>
/// <param name="pHist256">
/// Гистограмма однотонного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до len-1
/// </param>
/// <param name="len">
/// Размер массива.
/// Фактически является верхней границей яркости исходного монохромного изображения+1
/// </param>
float CVIEngineBase::MakeEntropyX(int* pHist256, int len)
{
    if(!pHist256)
        return 0;

    int i,m = 0;
    double flen = (double)len;
    double M,D,D2;

    double sV = 0,sVX = 0,sDV = 0,sDV2 = 0;
    for( i = 1 ; i < len; ++i)
    {
        int a = pHist256[i];
        if(!a)
            continue;
        ++ m;

        double v = (double)(a);
        double X = i/flen;

        sV  += v; // Накопление Сумма pHist256(i) = количество пикселей в исходном изображении
        sVX += v*X; // Накопление Сумма i*pHist256(i)/flen - сумма яркостей всех пикселей, если яркость записывать значениями от 0.0 до 1.0
    }

    if(m <= 1 || sV == 0)
        return 0;

    if(sV == 0)
        return 0;

    M = sVX/sV; // Среднее значение яркости (учитываются только ненулевые элементы), если яркость записывать значениями от 0.0 до 1.0

    for( i = 1 ; i < len; ++i)
    {
        double v = (double)(pHist256[i]);
        double X = i/flen;
        double d = X-M;
        double dd = d*d; // Вычесление коэффициента для i-го жначения
        sDV += dd*v; // Накопление Сумма dd(i)*pHist256(i)
		sDV2 += dd*dd*v; // Накопление Сумма dd(i)*dd(i)*pHist256(i)
    }

    D = sDV/sV;
    D2 = sDV2/sV;

    double E = D/sqrt(D2); // return Сумма dd(i)*pHist256(i) / sqrt( Сумма dd(i)*dd(i)*pHist256(i))

    return (float)E;
}

/// <summary>
/// Вычисление энтропии по гистограмме сигнала (Вариант S - заимствование идеи из ...)
/// Термин энтропия означает численное обозначение некоторой неопределённости и используется в различных областях науки для анализа данных
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// Сумма pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Сперва в таблице pHist256 находим элемент i с наиболее часто вcтречающейся яркостью 
/// и для него вычисляется Pm - количество таких пикселей и Xm = m/len = то есть значение яркости выраженное от 0.0 до 1.0
/// (максимальнвя яркость на данном монохромном изображении равна 1.0)
/// И вычисляется значение return log( Сумма abs( (i*fps/len - Xm) * ( pHist256(i) - Pm ) ) / len )
/// где  fps - количество кадров/сек из настроечных параметров алгоритмов (параметр VI_VAR_FPSOUTR)
/// В данной реализации элементы i, для которых pHist256(i) == 0 или i == 0, не участвуют в расчётах суммы
/// </summary>
/// <param name="pHist256">
/// Гистограмма однотонного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до len-1
/// </param>
/// <param name="len">
/// Размер массива.
/// Фактически является верхней границей яркости исходного монохромного изображения+1
/// </param>
float CVIEngineBase::MakeEntropyS(int* pHist256, int len)
{
    if(!pHist256)
        return 0;

    int m = 0,i,Pi,Pm=0;
    double Xi,Xm=0,sum = 0,flen = len;
    double fps = m_cfg.GetF1(VI_VAR_FPSOUTR);

    for( i = 1; i < len; ++i)
    {
        Xi = i*fps/flen;
        Pi = pHist256[i];
        if(!Pi)
            continue;
        if(Pi >= Pm)
            Pm = Pi, Xm = Xi;
        ++m;
    }
    if(!m)
        return 0;

    for( i = 1; i < len; ++i)
    {
        Xi = i*fps/flen;
        Pi = pHist256[i];
        if(!Pi)
            continue;

        sum += fabs( (Xm-Xi)*(double)(Pm-Pi) ); // Накопление суммы отклонений
    }

    if(sum == 0)
        return 0;

    return (float) log( sum/m ); // Согласно ранее приведённому коду m == len
}

/// <summary>
/// Загрузка монохромного изображения m_imgSrcMask из файла
/// Задание размера монохромного изображения m_imgSrcMask равным ширине и высоте изображения в файле
/// Заполнение монохромного изображения m_imgSrcMask значениями 0x00 и 0xFF на основании значений пикселей изображения в файле
/// При этом при заполнении m_imgSrcMask значениями используется вертикально перевёрнутое изображения из файла
/// В m_imgSrcMask записывается значение 0x00 если соответствующий пиксель имеет нулевое значение COLORREF (чёрный цвет)
/// и записывается значение 0xFF во всех остальных случаяж
/// The COLORREF value is used to specify an RGB color.
/// </summary>
/// <param name="file">Имя файла</param>
bool CVIEngineBase::SrcMaskLoad(LPCWSTR file)
{
    CImage img;
    if(img.Load(file) != S_OK)
        return false;
    int w = img.GetWidth();
    int h = img.GetHeight();

    CMTSingleLock   xlock(m_locks + LVI_SRC8, true);
    m_imgSrcMask.resize(w,h,false);

    CImageDC idc(img);

    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
        {
            COLORREF c = ::GetPixel(idc,x,h-1-y);
            m_imgSrcMask[y][x] = c ? 0: 0xFF;
        }

        return true;
}

/// <summary>
/// Сохранение монохромного изображения m_imgSrcMask в файле
/// Сперва каждому пикселю в m_imgSrcMask соответствует 32 бита данных в ОЗУ
/// Каждый байт монохромного изображения заменяется на 4 байта данных в ОЗУ
/// Заполнение файла производится только значениями 0x000000 и 0xFFFFFF на основании значений пикселей в m_imgSrcMask
/// В память записывается значение 0x00000000 если соответствующий пиксель в m_imgSrcMask имеет нулевое значение (чёрный цвет)
/// и записывается значение 0x00FFFFFF во всех остальных случаяж
/// Затем сформированный массив байт созраняется как изображение в формате соответствующем расширению имени файла
/// </summary>
/// <param name="file">Имя файла</param>
bool CVIEngineBase::SrcMaskSave(LPCWSTR file)
{
    CMTSingleLock   xlock(m_locks + LVI_SRC8, true);
    if(m_imgSrcMask.empty())
        return false;
    int w = m_imgSrcMask.w;
    int h = m_imgSrcMask.h;

    CImage img;
    img.Create(w,h,32);

    int pitch = img.GetPitch();
    DWORD *pBits = (DWORD *)img.GetBits();
    if(pitch < 0)
        pBits = (DWORD *)( ((BYTE*)pBits)+(h-1)*pitch);
    pitch = abs(pitch);

    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
        {
            pBits[y*w+x] =  m_imgSrcMask[y][x] ? 0:0xFFFFFF;
        }

        return (img.Save(file) == S_OK);
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::SrcMaskReset()
{
    CMTSingleLock   xlock(m_locks + LVI_SRC8, true);
    m_imgSrcMask.clear();
    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::SrcMaskErase(int x, int y)
{
    CMTSingleLock   xlock(m_locks + LVI_SRC8, true);

    int w = m_imgSrc8.w;
    int h = m_imgSrc8.h;
    if(x < 0 || x >= w || y < 0 || y >= h)
        return false;

    if(m_imgSrcMask.empty())
    {
        m_imgSrcMask.resize(w,h,false);
        m_imgSrcMask.set(m_imgSrcMask.p,255,m_imgSrcMask.s);
    }

    m_imgSrcMask[y][x] = 0;

    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeComN(int* pHist256, int len)
{
    int i;
    double x,y,xy,dx;       // x,y,x*y,M-x
    double M;   // X avg
    double S=0; // S (sqrt(D))
    double t;   // (x-M)/S
    double z;   // (1/2PI)^2*exp(-0.5*t^2)
    double SumY = 0, SumZ = 0, SumXY = 0;
    double nPI = (0.5/M_PI)*(0.5/M_PI);
    double K; // SumZ/SumY
    int cnt = 0;

    double vz[256]; // z = vz[i]

    ////////////////////////////////
    // Make M,SumX,SumXY
    for(i = 0; i < len; ++i)
    {
        if(!pHist256[i])
            continue;
        x = i;
        y = pHist256[i];
        xy = x*y;
        SumY += y;
        SumXY += xy;
        ++cnt;
    }

    if(SumY <= 0 || !cnt)
        return 0;

    M = SumXY/SumY;
    ////////////////////////////////

    ////////////////////////////////
    // Make S
    for(i = 0; i < len; ++i)
    {
        if(!pHist256[i])
            continue;
        x = i;
        y = pHist256[i];
        dx = x-M;
        S += dx*dx*y;
    }

    S = sqrt(S/SumY);

    if(S == 0)
        return 0;
    ////////////////////////////////

    ////////////////////////////////
    // Make K,z
    for(i = 0; i < len; ++i)
    {
        x = i;
        t = (x-M)/S;
        z = nPI*exp(-0.5*t*t);
        if(pHist256[i])
            SumZ += z;
        vz[i] = z;
    }

    K = (SumZ)/(SumY);
    //K = 1/(SumY);
    ////////////////////////////////

    ////////////////////////////////
    // Make Result
    double SumDYZ2 = 0; // sum( (yi-zi)^2 )
    double SumZ2 = 0; // sum( zi^2 )

    for(i = 0; i < len; ++i)
    {
        z = vz[i];
        if(pHist256[i])
        {
            double d;
            y = pHist256[i]*K;
            d = y-z;

            SumDYZ2 +=  d*d;
            SumZ2 += z*z;
        }
    }
    ////////////////////////////////

    double fDY = sqrt(SumDYZ2);
    double fZ = sqrt(SumZ2);

    float ret = (float)(1.0-fDY/fZ);

    if(ret < 0) ret = 0;
    if(ret > 1) ret = 1;

    return  ret;
}

/// <summary>
/// </summary>
/// <param name=""></param>
float CVIEngineBase::MakeComS(AURA_STAT& aura)
{
    int cnt = aura.line.size();
    int y;
    int nl = 0;

    float sC = 0, sW = 0;

    for(y = 0; y < cnt; ++y)
    {
        AURA_STAT_LINE& l = aura.line[y];
        if( !l.cwl && !l.cwr )
            continue;

        if(!l.cwl || !l.cwr)
            sC += 1, sW += 1;
        else
        {
            int mw = max(l.cwl,l.cwr);
            if(mw)
                sW += abs(l.cwl-l.cwr)/(float)mw;

            if(l.aColorL != l.aColorR)
                sC += abs(l.aColorL - l.aColorR)/255.0f;
        }

        ++nl;
    }

    if(!nl)
        return 0;

    return 1.0f - (sC+sW)/(2*nl);
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::GetAura(int * pCWL, int * pCWR, int * pCR, int * pCL, int nProc, bool bB)
{
    if(nProc >= (int)m_statRelease.size())
        return false;
    AURA_STAT &aura = bB ? m_statRelease[nProc].auraB : m_statRelease[nProc].auraA;

    int cnt = aura.line.size();

    for(int y = 0; y < cnt; ++y)
    {
        AURA_STAT_LINE& l = aura.line[y];
        if(pCWL) pCWL[y] = l.cwl;
        if(pCWR) pCWR[y] = l.cwr;

        if(pCL) pCL[y] = l.aColorL;
        if(pCR) pCR[y] = l.aColorR;
    }

    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::CanMakeHist(int id)
{
    return true;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::MakeFaceDraw(void)
{
    #ifndef SEQ_DISABLE_FACE
    if(!m_pFace)
        return;

    RGBQUAD*    imgRes;
    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);

    imgRes = (RGBQUAD*)(m_resultPtr[VI_RESULT_SRC_0]);
    if(imgRes)
        m_pFace->MakeDraw(imgRes,w,h);

    imgRes = (RGBQUAD*)(m_resultPtr[VI_RESULT_SRC_A]);
    if(imgRes)
        m_pFace->MakeDraw(imgRes,w,h);

    imgRes = (RGBQUAD*)(m_resultPtr[VI_RESULT_SRC_B]);
    if(imgRes)
        m_pFace->MakeDraw(imgRes,w,h);
    #endif
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineBase::NewSource()
{
    m_audio.NewSource();

    CMTSingleLock   lock(m_locks + LVI_ALL, true);
    Reset(true);

    m_cfg.PutI1(VI_VAR_NFRAME,0);
    m_cfg.PutI1(VI_VAR_NFRAME_IN,0);
    m_cfg.PutI1(VI_VAR_NFRAME_OUTF,0);

    m_tVideoDT = 0;
    m_cfg.PutI1(VI_VAR_NDROP,0);

}
