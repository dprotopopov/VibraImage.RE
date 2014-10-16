#include "StdAfx.h"
#include "VIEngine.h"
#include "VIEngineBase.h"
#include ".\vienginethread.h"
#include ".\vienginesimple.h"
#include "pointsse.h"
#include <math.h>
#include ".\guardantcheck7.h"
#include "VIEngineFace.h"

/// <summary>
/// Конструктор класса
/// </summary>
/// <param name="pBase">Указатель на класс реализаций алгоритмов программы</param>
CVIEngineThread::CVIEngineThread(CVIEngineBase *pBase) :
m_pBase(pBase)
,m_hThread(0)
,m_nId(0)
,m_yS(0)
,m_yE(0)
,m_pCfg(&(pBase->m_cfg))
{
    //  m_seqPass = m_seqCode = rand();
}

/// <summary>
/// </summary>
/// <param name=""></param>
CVIEngineThread::~CVIEngineThread(void)
{
    Stop();
}

/// <summary>
/// </summary>
/// <param name=""></param>
DWORD CVIEngineThread::ThreadProc(LPVOID lpParameter)
{
    CVIEngineThread *pThis = (CVIEngineThread*)lpParameter;
    pThis->Main();
    return 0;
}

/// <summary>
/// </summary>
void CVIEngineThread::Main()
{
    Init();

    SEQ_XOR_SYNC(m_pBase);

    bool bDone = false;

    while(!bDone)
    {
        int ev = WaitForMultipleObjects(EVI_CNT,
                                        (HANDLE*)m_events,false,INFINITE);
        m_events[ev].Reset();

        switch(ev)
        {
            case EVI_DONE:
                bDone = true;
                break;
            case EVI_ADDF:
                OnEventAddF();
                break;
            case EVI_DELTA:
                OnEventDelta();
                break;
            case EVI_SUM:
                OnEventSumm();
                break;
            case EVI_SUM_FILTER:
                OnEventSumFilter();
                break;
            case EVI_SUM_STAT:
                OnEventSumStat();
                break;
            case EVI_AURA:
                OnEventAura();
                break;
            case EVI_RESULT:
                SEQ_THREAD(m_pBase);
                OnEventResult();
                break;
        }
        m_evReady.Set();
    }

}

/// <summary>
/// </summary>
bool CVIEngineThread::Start(void)
{
    m_hThread = CreateThread(0,1024*1024,ThreadProc,this,0,&m_nId);
    return (m_hThread != 0);
}

/// <summary>
/// </summary>
void CVIEngineThread::Stop(void)
{
    if(!m_hThread)
        return;
    m_events[EVI_DONE].Set();
    WaitForSingleObject(m_hThread,INFINITE);
    m_hThread = 0;
    m_evReady.Set();
}

/// <summary>
/// </summary>
void CVIEngineThread::OnEventDelta(void)
{
    ///////////////////////////////////////////////////////////////
    // установка исходных значений
    ///////////////////////////////////////////////////////////////
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);

    ilFRAME_IMG iSrc = m_pBase->m_arrSrc.begin();
    if(m_pBase->m_arrSrc.size() <= 1)
        return;
    FRAME_IMG&  imgSrcF = *iSrc;
    FRAME_IMG&  imgPrvSrcF = *(++iSrc);

    int yS = m_yS,yE = m_yE;
    if(!yS) yS += 2;
    if(yE == h) yE -= 2;

    int cnt = (yE-yS)*w;

    float * pSrcF = imgSrcF.i[0] + yS*w;
    float * pSrcFE = imgSrcF.i[0] + yE*w;

    float * pPrvSrcF = imgPrvSrcF.i[0] + yS*w;

    ilFRAME_IMG iDelta = m_pBase->m_arrDelta.begin();
    FRAME_IMG&  imgDeltaF = *iDelta;

    float * pDeltaA = imgDeltaF.i[0] + yS*w;

    ///////////////////////////////////////////////////////////////

    float filterCTf = m_pBase->m_cfg.GetF1(VI_FILTER_CT);
    bool filterCT = (CFloatSSE(filterCTf) > CFloatSSE(0.0f))?true:false;

    bool filterDeltaStretch = m_pCfg->GetI1(VI_FILTER_DELTA_STRETCH)?true:false;


    CPointSSE   pntCur,pntPrev,delta,dsumA,dsumB;
    CPointSSE   filterDeltaLO(m_pCfg->GetF1(VI_FILTER_DELTA_LO));
    CPointSSE   v1(1.0f);
    CPointSSE   v0(0.0f);
    CPointSSE   fStretch = CPointSSE(255.0f)/(CPointSSE(255.0f)-filterDeltaLO);

    dsumA.set0();
    dsumB.set0();

    if(m_pCfg->GetI1(VI_VAR_NFRAME) )
    {
        while( pSrcF != pSrcFE )
        {
            pntCur = pSrcF;     // загружаю блок из 4х текущих точек
            pntPrev = pPrvSrcF; // загружаю блок из 4х предыдущих точек


            delta = pntCur - pntPrev;   // разность с предыдущим кадром
            delta.abs();                // модуль разности


            if(filterDeltaStretch) // шумовой фильтр + растягивание на 0..255
            {
                delta -= filterDeltaLO;
                delta.limit_lo(v0);
                delta *= fStretch;
            } else
                delta.limit_lo(filterDeltaLO);  // шумовой фильтр

                if( filterCT )
                    CVIEngineSimple::FilterCT(delta,pSrcF,w,filterCTf);

                dsumA +=delta;      // интегральная
                delta.export2c(pDeltaA);    // сохраняю дельту A

                delta.mask_nz(v1);      // deltaB = (deltaA != 0) ? 1 : 0;

            dsumB += delta;     // интегральная

            pSrcF += 4;
            pPrvSrcF += 4;
            pDeltaA += 4;
        }
    } else
    {
        // для первого кадра делта=0
        delta.set0();

        while( pSrcF != pSrcFE )
        {
            delta.export2c(pDeltaA);        // сохраняю дельту A

            pSrcF += 4;
            pDeltaA += 4;
        }
    }

    if(!m_yS)
        pDeltaA = 0;

    UINT mStatSize = m_stat.size();
    float dsumAsum = dsumA.sum();
    float dsumBsum = dsumB.sum();

    for(UINT k = 0; k < mStatSize; ++k)
    {
        m_stat[k].dsumA = dsumAsum;
        m_stat[k].dsumB = dsumBsum;
    }

    _mm_empty();
}


/// <summary>
/// </summary>
void CVIEngineThread::OnEventAddF(void)
{
    //////////////////////////////////////////////////////////////
    // установка исходных значений
    ///////////////////////////////////////////////////////////////
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w;

    ilFRAME_IMG iSrc = m_pBase->m_arrSrc.begin();
    FRAME_IMG&  imgSrcF = *iSrc;
    FRAME_IMG&  imgPrvSrcF = *(++iSrc);

    float * pCurF = m_pBase->m_imgSrcF + m_yS*w;
    float * pSrcF = imgSrcF.i[0] + m_yS*w;
    float * pSrcFE = imgSrcF.i[0] + m_yE*w;

    CPointSSE v;
    while( pSrcF != pSrcFE )
    {

        v = pCurF;
        v.export2c(pSrcF);
        pCurF += 4;
        pSrcF += 4;
    }
    _mm_empty();
}


/// <summary>
/// </summary>
void CVIEngineThread::OnEventSumm(void)
{
    BOOL disableA = m_pCfg->GetI1(VI_FILTER_DISABLE_A);
    BOOL disableB = m_pCfg->GetI1(VI_FILTER_DISABLE_B);
    if(disableA && disableB)
        return;

    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w,k;
    int x,y;
    int nSumm = m_pBase->m_summ.size();

    SUMM_PTR*   pSumm=0;
    if(nSumm)
    {
        pSumm = new SUMM_PTR[nSumm];
        for( k = 0; k < nSumm; ++k )
            MakeSummPtr(k,pSumm+k);
    }

    ilFRAME_IMG iDelta = m_pBase->m_arrDelta.begin();
    FRAME_IMG&  imgDeltaF = *iDelta;
    FRAME_IMG&  imgPrvDeltaF = *(++iDelta);


    float fthv = m_pCfg->GetF1(VI_VAR_TH);
    bool fthb = ((*(DWORD*)&fthv) != 0);

    CPointSSE delta,ldelta,summ;
    CPointSSE fth( fthv );
    CPointSSE   v1(1.0f);


    for( k = 0; k < nSumm; ++k )
    {
        BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
        if(disableSum)
            continue;

        float * pDeltaA = imgDeltaF.i.p + m_yS*w;

        SUMM_PTR& S = pSumm[k];

        CPointSSE   SdivA(S.divA);
        CPointSSE   SdivB(S.divB);

        for( y = m_yS; y < m_yE; ++y)
        {
            for( x = 0; x < w; x += 4)
            {
                ///////////////////////////////////
                /// SUMM A
                ///////////////////////////////////
                delta = pDeltaA;

                float *pl = S.pCDeltaA;

                {
                    float *ps = S.pCSummA;

                    summ = ps;
                    summ += delta;

                    if(pl)
                    {
                        ldelta = pl;
                        summ -= ldelta;
                        S.pCDeltaA += 4;
                    } else
                        ldelta.set0();

                    if(!disableA)
                    {
                        summ.export2c(ps);

                        summ *= SdivA;

                        if(fthb)
                            summ.limit_lo(fth); // 080201
                            summ.export2c(S.pCRetA);
                        S.pCRetA += 4;
                    }

                    S.pCSummA += 4;
                }

                pDeltaA += 4;

                ///////////////////////////////////
                /// SUMM B
                ///////////////////////////////////
                if(!disableB)
                {
                    delta.mask_nz(v1);                                          ldelta.mask_nz(v1);
                    float *ps = S.pCSummB;

                    summ = ps;
                    summ += delta;
                    summ -= ldelta;

                    summ.export2c(ps);

                    summ *= SdivB;
                    if(fthb)
                        summ.limit_lo(fth);

                    summ.export2c(S.pCRetB);

                    S.pCSummB += 4;
                    S.pCRetB += 4;
                }
            }
        }
    }
    _mm_empty();

    delete pSumm;
}



/// <summary>
/// </summary>
void CVIEngineThread::OnEventResult(void)
{
	int res = m_pCfg->GetI1(VI_MODE_RESULT);

    if( (res & VI_RESULT_SRC_0) != 0)
        MakeResultSrc(VI_RESULT_SRC_0);
    if( (res & VI_RESULT_SRC_A) != 0)
        MakeResultSrc(VI_RESULT_SRC_A);
    if( (res & VI_RESULT_SRC_B) != 0)
        MakeResultSrc(VI_RESULT_SRC_B);

    if( (res & VI_RESULT_VI0_A) != 0)
        MakeResultVI(VI_RESULT_VI0_A);
    if( (res & VI_RESULT_VI0_B) != 0)
        MakeResultVI(VI_RESULT_VI0_B);

    if( (res & VI_RESULT_VI1_A) != 0)
        MakeResultVI(VI_RESULT_VI1_A);
    if( (res & VI_RESULT_VI1_B) != 0)
        MakeResultVI(VI_RESULT_VI1_B);

    if( (res & VI_RESULT_VI2_A) != 0)
        MakeResultVI(VI_RESULT_VI2_A);
    if( (res & VI_RESULT_VI2_B) != 0)
        MakeResultVI(VI_RESULT_VI2_B);

    if( (res & VI_RESULT_DELTA_A) != 0)
        MakeResultDelta(VI_RESULT_DELTA_A);
    if( (res & VI_RESULT_DELTA_B) != 0)
        MakeResultDelta(VI_RESULT_DELTA_B);

}

/// <summary>
/// </summary>
void CVIEngineThread::MakeSummPtr(int n, CVIEngineThread::SUMM_PTR* pSumm)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);

    int n0 = 0;

    n0 = m_pBase->m_arrDelta.front().n;

    int nL = 0;
    pSumm->n = m_pBase->m_cfg.GetI1(m_pBase->m_summ[n].id);
    pSumm->nLast = n0 - pSumm->n+1;
    pSumm->pDeltaA = 0;
    pSumm->pCDeltaA = 0;

    int nDiv = pSumm->n-1;

    if(pSumm->nLast >= 0)
    {
        ilFRAME_IMG iDelta;
        for(iDelta = m_pBase->m_arrDelta.begin();
            iDelta != m_pBase->m_arrDelta.end(); ++iDelta)
            {
                FRAME_IMG & delta = *iDelta;
                if(delta.n == pSumm->nLast)
                {
                    pSumm->pDeltaA = delta.i[0];
                    break;
                }
            }
    } else
    {
        nDiv = m_pBase->m_cfg.GetI1(VI_VAR_NFRAME)+1;
    }

    pSumm->pStat = & (m_pBase->m_stat[n]);

    SUM_IMG & summ = m_pBase->m_summ[n];
    pSumm->pSummA = summ.i[0];
    pSumm->pSummB = summ.i[h];
    pSumm->pRetA = summ.i[h*2];
    pSumm->pRetB = summ.i[h*3];

    pSumm->pRetIA = summ.si[0];
    pSumm->pRetIB = summ.si[h];

    pSumm->pCSummA = pSumm->pSummA + m_yS*w;
    pSumm->pCSummB = pSumm->pSummB + m_yS*w;

    pSumm->pCRetA = pSumm->pRetA + m_yS*w;
    pSumm->pCRetB = pSumm->pRetB + m_yS*w;

    pSumm->pCRetIA = pSumm->pRetIA + m_yS*w;
    pSumm->pCRetIB = pSumm->pRetIB + m_yS*w;

    if(pSumm->pDeltaA)
        pSumm->pCDeltaA = pSumm->pDeltaA + m_yS*w;

    CFloatSSE tmp;

    float fnDiv = (float)nDiv;

    float aDiv = m_pBase->m_cfg.GetF1(VI_FILTER_AM);
    if(aDiv > 0)
        pSumm->divA = aDiv / fnDiv;
    else
        pSumm->divA = 1.0f;

    pSumm->divB = 255.0f / fnDiv;
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::Init(void)
{
    m_stat.resize(m_pBase->m_stat.size());
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    for(UINT k = 0; k < m_stat.size(); ++k)
    {
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::ClearStat(void)
{
    for(UINT k = 0; k < m_stat.size(); ++k)
    {
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultSrc(int res)
{
    if(m_pCfg->GetI1(VI_MODE_COLOR) && !m_pBase->m_imgSrc24.empty())
        MakeResultSrc24(res);
    else
        MakeResultSrc8(res);
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultSrc24(int res)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w;

    bool bAura = true;

    RGBQUAD*    imgRes;
    if(res & VI_RESULT_SRC_0)
    {
        imgRes = (RGBQUAD*)(m_pBase->m_resultPtr[VI_RESULT_SRC_0]);
        bAura = false;
    }
    else
        if(res & VI_RESULT_SRC_A)
        {
            imgRes = (RGBQUAD*)(m_pBase->m_resultPtr[VI_RESULT_SRC_A]);
        }
        else
            if(res & VI_RESULT_SRC_B)
            {
                imgRes = (RGBQUAD*)(m_pBase->m_resultPtr[VI_RESULT_SRC_B]);
            }
            else
                return;

            if(!imgRes)
                return;

            #ifndef SEQ_DISABLE_DISTORTION
            imgRes = m_pBase->m_Distortion.PreparePtr(imgRes);
            #endif // #ifndef SEQ_DISABLE_DISTORTION

            RGBQUAD *   pRes    = imgRes + m_yS*w, *pResE = imgRes + m_yE*w;

            RGBTRIPLE * imgSrc = m_pBase->m_imgSrc24.p;
            RGBTRIPLE * pSrc = imgSrc + m_yS*w;

            while(pRes != pResE)
            {
                pRes->rgbBlue = pSrc->rgbtBlue;
                pRes->rgbGreen = pSrc->rgbtGreen;
                pRes->rgbRed = pSrc->rgbtRed;
                pRes->rgbReserved = 0;
                ++pRes;
                ++pSrc;
            }

            if(bAura && !m_pBase->m_bLock)
            {
                if( ! m_pCfg->GetI1(VI_MACRO_AURA) )
                    MakeResultVIDraw(0,(DWORD*)imgRes,res);
                else
                    if( !m_pBase->m_statRelease.empty())
                        MakeResultAuraDrawMix(0,(DWORD*)imgRes,res);
            }

            #ifndef SEQ_DISABLE_DISTORTION
            m_pBase->m_Distortion.Make(m_yS,m_yE);
            #endif // #ifndef SEQ_DISABLE_DISTORTION
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultSrc8(int res)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w;

    bool bAura = true;

    RGBQUAD*    imgRes;
    if(res & VI_RESULT_SRC_0)
    {
        imgRes = (RGBQUAD*)(m_pBase->m_resultPtr[VI_RESULT_SRC_0]);
        bAura = false;
    }
    else
        if(res & VI_RESULT_SRC_A)
        {
            imgRes = (RGBQUAD*)(m_pBase->m_resultPtr[VI_RESULT_SRC_A]);
        }
        else
            if(res & VI_RESULT_SRC_B)
            {
                imgRes = (RGBQUAD*)(m_pBase->m_resultPtr[VI_RESULT_SRC_B]);
            }
            else
                return;

            if(!imgRes)
                return;

            #ifndef SEQ_DISABLE_DISTORTION
            imgRes = m_pBase->m_Distortion.PreparePtr(imgRes);
            #endif // #ifndef SEQ_DISABLE_DISTORTION

            RGBQUAD *   pRes    = imgRes + m_yS*w, *pResE = imgRes + m_yE*w;

            BYTE * imgSrc = m_pBase->m_imgSrc8;
            BYTE * pSrc = imgSrc + m_yS*w;

            while(pRes != pResE)
            {
                pRes->rgbBlue = pRes->rgbGreen = pRes->rgbRed = *pSrc;
                pRes->rgbReserved = 0;
                ++pRes;
                ++pSrc;
            }


            if(bAura && !m_pBase->m_bLock)
            {
                if( ! m_pCfg->GetI1(VI_MACRO_AURA) )
                    MakeResultVIDraw(0,(DWORD*)imgRes,res);
                else
                    if( !m_pBase->m_statRelease.empty())
                        MakeResultAuraDrawMix(0,(DWORD*)imgRes,res);
            }

            #ifndef SEQ_DISABLE_DISTORTION
            m_pBase->m_Distortion.Make(m_yS,m_yE);
            #endif // #ifndef SEQ_DISABLE_DISTORTION
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultVI(int res)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w;

    int id_vi = res;
    int n = res2n(res);

    DWORD*  imgRes  = (DWORD*)(m_pBase->m_resultPtr[id_vi]);


    if(!imgRes)
        return;
    #ifndef SEQ_DISABLE_DISTORTION
    imgRes = m_pBase->m_Distortion.PreparePtr(imgRes);
    #endif // #ifndef SEQ_DISABLE_DISTORTION

    DWORD * pRes    = imgRes + m_yS*w, *pResE = imgRes + m_yE*w;
    int nSum = m_pBase->m_summ.size();
    if( nSum <= n || n < 0)
    {
        SSEmemset(pRes,0,cnt*sizeof(RGBQUAD));
        return;
    }

    SUM_IMG &sum = m_pBase->m_summ[n];
    short * imgSum;

    if(IsModeA(res))
        imgSum = sum.si[0];
    else
        imgSum = sum.si[h];


    USHORT *pSum = (USHORT *)(imgSum + m_yS*w);


    DWORD pal[256];
    CopyMemory(pal,(DWORD*)m_pBase->m_palI,sizeof(pal));
    pal[0] = (m_pBase->m_cfg.GetI1(VI_MODE_WBG)&id_vi)?0xFFFFFF:0;

    while(pRes != pResE)
    {
        *pRes = pal[ *pSum ];

        ++pSum;
        ++pRes;
    }

    MakeResultAuraDraw(n,imgRes,id_vi);

    #ifndef SEQ_DISABLE_DISTORTION
    m_pBase->m_Distortion.Make(m_yS,m_yE);
    #endif // #ifndef SEQ_DISABLE_DISTORTION
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultDelta(int res)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w;

    DWORD*  imgRes;
    if(res == VI_RESULT_DELTA_A)
        imgRes = (DWORD*)(m_pBase->m_resultPtr[VI_RESULT_DELTA_A]);
    else
        if(res == VI_RESULT_DELTA_B)
            imgRes = (DWORD*)(m_pBase->m_resultPtr[VI_RESULT_DELTA_B]);
        else
            return;

        DWORD * pRes    = imgRes + m_yS*w, *pResE = imgRes + m_yE*w;
    if(!imgRes)
        return;

    ilFRAME_IMG iDelta = m_pBase->m_arrDelta.begin();
    FRAME_IMG&  imgDeltaF = *iDelta;

    float * pDelta = 0;
    pDelta = imgDeltaF.i.p + m_yS*w;
    CPointSSE p,v255(255.0f);

    __m64 idx;

    DWORD pal[256];
    CopyMemory(pal,(DWORD*)m_pBase->m_palI,sizeof(pal));
    pal[0] = (m_pBase->m_cfg.GetI1(VI_MODE_WBG)&res)?0xFFFFFF:0;

    bool bModeB = IsModeB(res);

    while(pRes != pResE)
    {
        p = pDelta;
        if( bModeB )
            p.mask_nz(v255);
        else
            p.limit_hi(v255);

        idx = _mm_cvtps_pi16(p.p);

        pRes[0] = pal[ idx.m64_u16[0] ];
        pRes[1] = pal[ idx.m64_u16[1] ];
        pRes[2] = pal[ idx.m64_u16[2] ];
        pRes[3] = pal[ idx.m64_u16[3] ];

        pDelta += 4;
        pRes += 4;
    }

    _mm_empty();

    MakeResultAuraDraw(0,(DWORD*)imgRes,res);
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::OnEventSumFilter(void)
{
    int nSumm = m_pBase->m_summ.size();
    UINT i;
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);


    SUMM_PTR*   pSumm=0;
    if(nSumm)
    {
        pSumm = new SUMM_PTR[nSumm];
        for( UINT i = 0; i < m_nSum.size(); ++i )
        {
            int k = m_nSum[i];

            BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
            if(disableSum)
                continue;
            MakeSummPtr(k,pSumm+k);
        }
    } else
        return;

    if(m_pCfg->GetI1(VI_FILTER_SP))
    {
        for( i = 0; i < m_nSum.size(); ++i )
        {
            int k = m_nSum[i];
            BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
            if(disableSum)
                continue;

            SUMM_PTR& S = pSumm[k];
            FilterSP(S.pRetA);
            FilterSP(S.pRetB);
        }
    }


    for( i = 0; i < m_nSum.size(); ++i )
    {
        VI_VAR v;
        int k = m_nSum[i];
        bool okX,okY;

        BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
        if(disableSum)
            continue;

        SUMM_STAT& SS = m_pBase->m_stat[k];
        SUMM_PTR& S = pSumm[k];

        MakeIntResult(S.pRetA,S.pRetIA,SS.xhistAX.m_hist,SS.xhistAY.m_hist,w,h);
        okX = SS.xhistAX.Make();
        okY = SS.xhistAY.Make();

        if(okX && okY)
        {
            v.fv1 = SS.xhistAX.m_cxF;
            v.fv2 = SS.xhistAY.m_cxF;
            v.iv1 = SS.xhistAX.m_cxI;
            v.iv2 = SS.xhistAY.m_cxI;
            m_pCfg->PutVar(VI_VAR_HIST_CX_AVG_A0+k,v);
        }

        MakeIntResult(S.pRetB,S.pRetIB,SS.xhistBX.m_hist,SS.xhistBY.m_hist,w,h);
        okX = SS.xhistBX.Make();
        okY = SS.xhistBY.Make();

        if(okX && okY)
        {
            v.fv1 = SS.xhistBX.m_cxF;
            v.fv2 = SS.xhistBY.m_cxF;
            v.iv1 = SS.xhistBX.m_cxI;
            v.iv2 = SS.xhistBY.m_cxI;
            m_pCfg->PutVar(VI_VAR_HIST_CX_AVG_B0+k,v);
        }
    }

    delete pSumm;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::FilterSP(float *p)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int x,y,w1=w-1,h1=h-1;

    if(m_tmp.w != w || m_tmp.h != h)
        m_tmp.resize(w,h,false);

    SSEmemcpy( m_tmp.p,p,w*h*sizeof(float) );


    float vZ = 1.0f;

    for( y = 1; y < h1; ++y)
    {
        int yw = y*w;
        float *py = p+yw;
        float *pt = m_tmp.p+yw;

        py[0] = py[w-1] = 0;


        for( x = 1; x < w1; ++x,++pt)
        {
            int cnt = 0;

            if(pt[ 1] < vZ) ++cnt;
            if(pt[-1] < vZ) ++cnt;
            if(pt[ w] < vZ) ++cnt;
            if(pt[-w] < vZ) ++cnt;

            if(cnt >= 2)
                py[x] = 0;
        }
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::FilterSPsse(float *p)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int x,y,w1=w-1,h1=h-1;

    if(m_tmp.w != w || m_tmp.h != h)
        m_tmp.resize(w,h,false);

    SSEmemcpy( m_tmp.p,p,w*h*sizeof(float) );


    CPointSSE vZ(1.0f),vt;

    for( y = 1; y < h1; ++y)
    {
        int yw = y*w;
        float *py = p+yw;
        float *pt = m_tmp.p+yw;

        py[0] = py[w-1] = 0;


        for( x = 0; x < w; x +=4,pt+=4,py+=4)
        {
            int cnt[4] = {0,0,0,0};
            int m;
            ////////////////////////////////////////////
            vt = (pt-w);
            vt.p = _mm_cmpgt_ps(vZ.p,vt.p);
            m = vt.mask();

            if(m&1) cnt[0]++;
            if(m&2) cnt[1]++;
            if(m&4) cnt[2]++;
            if(m&5) cnt[3]++;
            ////////////////////////////////////////////

            ////////////////////////////////////////////
            vt = (pt+w);
            vt.p = _mm_cmpgt_ps(vZ.p,vt.p);
            m = vt.mask();

            if(m&1) cnt[0]++;
            if(m&2) cnt[1]++;
            if(m&4) cnt[2]++;
            if(m&5) cnt[3]++;
            ////////////////////////////////////////////

            ////////////////////////////////////////////
            vt.uload(pt-1);
            vt.p = _mm_cmpgt_ps(vZ.p,vt.p);
            m = vt.mask();

            if(m&1) cnt[0]++;
            if(m&2) cnt[1]++;
            if(m&4) cnt[2]++;
            if(m&5) cnt[3]++;
            ////////////////////////////////////////////

            ////////////////////////////////////////////
            vt.uload(pt+1);
            vt.p = _mm_cmpgt_ps(vZ.p,vt.p);
            m = vt.mask();

            if(m&1) cnt[0]++;
            if(m&2) cnt[1]++;
            if(m&4) cnt[2]++;
            if(m&5) cnt[3]++;
            ////////////////////////////////////////////

            if(cnt[0] >= 2)
                py[0] = 0;
            if(cnt[1] >= 2)
                py[1] = 0;
            if(cnt[2] >= 2)
                py[2] = 0;
            if(cnt[3] >= 2)
                py[3] = 0;
        }
    }
    _mm_empty();
}



/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::OnEventAura(void)
{
    int cnt = m_nSum.size();

    bool bProcA = (!m_pCfg->GetI1(VI_FILTER_DISABLE_A));
    bool bProcB = (!m_pCfg->GetI1(VI_FILTER_DISABLE_B));

    for( int n = 0; n < cnt; ++n )
    {
        int k = m_nSum[n];
        BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
        if(disableSum)
            continue;

        if(bProcA)
            MakeAuraV6( k,false );
        if(bProcB)
            MakeAuraV6( k,true );
    }
}


/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeAuraV6(int nSum, bool bModeB)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    if(!w || !h)
        return;

    SUMM_PTR    S;
    MakeSummPtr(nSum,&S);

    if(!bModeB)
    {
        if( m_pBase->m_aura6A.size() > (size_t)nSum)
        {
            CVIEngineAura6& aura6 = m_pBase->m_aura6A[nSum];

            aura6.Make(S.pRetIA,w,h);
            aura6.MakeExport(S.pStat->auraA);
        }
    }
    else
    {
        if( m_pBase->m_aura6B.size() > (size_t)nSum)
        {
            CVIEngineAura6& aura6 = m_pBase->m_aura6B[nSum];

            aura6.Make(S.pRetIB,w,h);
            aura6.MakeExport(S.pStat->auraB);
        }
    }

    MakeAuraStat(nSum,bModeB);
}




/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeAuraStat(int nSum, bool bModeB)
{
    AURA_STAT & stat = bModeB ? m_pBase->m_stat[nSum].auraB : m_pBase->m_stat[nSum].auraA;
    float* pS0 = GetSumPtr(nSum,bModeB);
    short* pS0I = GetSumPtrI(nSum,bModeB);

    int w,h,y;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);

    mmx_array<int>  &statHist = m_tmp_aura.statHist;    // гистограмма цветов
    mmx_array<int>  &statHistA  = m_tmp_aura.statHistA; // гистограмма цветов по кадру
    mmx_array<int>  &statHistW = m_tmp_aura.statHistW;  // гистограмма цветов*ширину
    mmx_array<int>  &statHistC = m_tmp_aura.statHistC;  // гистограмма цветов в контуре
    mmx_array<short> &iLine = m_tmp_aura.iLine;         // округленный цвет

    statHist.resize(256);
    statHistA.resize(256);
    statHistW.resize(256);
    statHistC.resize(256);
    iLine.resize(w);

    int *pl = stat.outlineL.begin();
    int *pr = stat.outlineR.begin();

    stat.line.set0();
    ZeroMemory(&stat.sline,sizeof(stat.sline));

    if(stat.imgCenterX <= 0 || stat.imgCenterX >= w-1)
        return;
    /// проход 1
    SEQ_XOR_VALUE(m_pBase);

    float cLMin = 10000, cLMax = 0; // для опр. симметрии
    for( y = 0; y < h; ++y )
    {
        float *psy = pS0 + y*w;

        CVIEngineSimple::RoundLine(psy,iLine.p,w);
        CVIEngineSimple::AddHist(statHistA.p,iLine.p,w);

        #ifndef SEQ_DISABLE_FACE
        if(!nSum && bModeB && m_pBase->m_pFace)
            m_pBase->m_pFace->MakeStatLine(y,iLine.p,w);
        #endif

        AURA_STAT_LINE& line = stat.line[y];

        int cla=1,cra=1;

        line.xl = line.xla = pl[y];
        line.xr = line.xra = pr[y];

        SEQ_XOR_MAKE(&line.xl);
        SEQ_XOR_MAKE(&line.xr);

        if(line.xl > 0)
        {
            line.tl = stat.imgCenterX-line.xl;
            line.pl = psy + line.xl;
            CVIEngineSimple::sum_cnt_max_4(line.xl,line.pl,line.tl,&line.sumL,&line.maxL,&line.maxLx,&line.cl,statHistC.p);

            stat.sline.lcntL ++;
            stat.sline.cl += line.cl;
            stat.sline.sumL += line.sumL;
            stat.sline.maxL = max(stat.sline.maxL,line.maxL);
            stat.sline.xl += line.xl;

            line.maxLi = round(line.maxL);
            if(line.maxLi > 255) line.maxLi = 255;
            if(line.maxLi < 0) line.maxLi = 0;

            stat.sline.maxLi = round(stat.sline.maxL);
            if(stat.sline.maxLi > 255) stat.sline.maxLi = 255;
            if(stat.sline.maxLi < 0) stat.sline.maxLi = 0;

            stat.statCM += line.maxL, ++stat.statCnt;

            cLMin = min( cLMin, line.maxL );
            cLMax = max( cLMax, line.maxL );

            if(y > 0 && pl[y-1] >= 0)
                line.xla += pl[y-1], ++cla;
            if(y < h-1 && pl[y+1] >= 0)
                line.xla += pl[y+1], ++cla;
            line.xla /= cla;

            if(line.cl)
                line.cwl =  round( line.sumL/line.cl );
            else
                line.cwl = 0;

            if(bModeB)
                line.aColorL = line.maxLi;
            else
            {
                line.aColorL = MakeAuraColorA(nSum,line.maxLx,y);
            }
            if(line.aColorL > stat.sline.aColorL)
                stat.sline.aColorL = line.aColorL;

            statHist[line.aColorL] ++;
            statHistW[line.aColorL] += line.cwl;

            stat.sline.cwl += line.cwl;
        }
        if(line.xr > 0)
        {
            line.tr = line.xr-stat.imgCenterX;
            line.pr = psy + stat.imgCenterX;
            CVIEngineSimple::sum_cnt_max_4(stat.imgCenterX,line.pr,line.tr,&line.sumR,&line.maxR,&line.maxRx,&line.cr,statHistC.p);

            stat.sline.lcntR ++;
            stat.sline.cr += line.cr;
            stat.sline.sumR += line.sumR;
            stat.sline.maxR = max(stat.sline.maxR,line.maxR);
            stat.sline.xr += line.xr;

            line.maxRi = round(line.maxR);
            if(line.maxRi > 255) line.maxRi = 255;
            if(line.maxRi < 0) line.maxRi = 0;
            stat.sline.maxRi = round(stat.sline.maxR);
            if(stat.sline.maxRi > 255) stat.sline.maxRi = 255;
            if(stat.sline.maxRi < 0) stat.sline.maxRi = 0;

            cLMin = min( cLMin, line.maxR );
            cLMax = max( cLMax, line.maxR );

            stat.statCM += line.maxR, ++stat.statCnt;

            if(y > 0 && pr[y-1] >= 0)
                line.xra += pr[y-1], ++cra;
            if(y < h-1 && pr[y+1] >= 0)
                line.xra += pr[y+1], ++cra;
            line.xra /= cra;

            if(line.cr)
                line.cwr =  round( line.sumR/line.cr );
            else
                line.cwr = 0;

            if(bModeB)
                line.aColorR = line.maxRi;
            else
                line.aColorR = MakeAuraColorA(nSum,line.maxRx,y);

            if(line.aColorR > stat.sline.aColorR)
                stat.sline.aColorR = line.aColorR;

            statHist[line.aColorR] ++;
            statHistW[line.aColorR] += line.cwr;

            stat.sline.cwr += line.cwr;
        }
    }

    if( stat.statCnt )
    {
        stat.statCM /= (float)stat.statCnt;
        if(cLMax > cLMin)
        {
            float divSim = (cLMax - stat.statCM);
            if( divSim > 0)
                stat.statSim = (stat.statCM - cLMin)/divSim;
        }
    }
    /// проход 2
    for( y = 0; y < h; ++y )
    {
        AURA_STAT_LINE& line = stat.line[y];
        float d;
        if(line.xl > 0)
        {
            d = line.maxL - stat.statCM;
            stat.statCD += d*d;
        }
        if(line.xr > 0)
        {
            d = line.maxR - stat.statCM;
            stat.statCD += d*d;
        }
    }

    if( stat.statCnt )
    {
        stat.statCD /= (float)stat.statCnt;
        stat.statCS = sqrtf(stat.statCD);
    }

    if(m_pCfg->GetI1(VI_VAR_STAT_CFG_HISTFN))
    {
        CVIEngineSimple::MakeHistFn(statHist,m_pCfg->GetI1(VI_VAR_STAT_CFG_HISTFN),bModeB);
        statHistW = statHistA = statHistC = statHist;
    }

    statHist[0] = statHistA[0] = statHistW[0] = statHistC[0] = 0;
    stat.statHist = statHist;
    stat.statHistA = statHistA;
    stat.statHistW = statHistW;
    stat.statHistC = statHistC;

    MakeAuraStatHist(stat);
    MakeAuraStatTransform(stat);
}

/// <summary>
/// </summary>
/// <param name=""></param>
float* CVIEngineThread::GetSumPtr(int nSum, bool bModeB)
{
    SUM_IMG &sum = m_pBase->m_summ[nSum];
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    if(!bModeB) return sum.i[h*2];
    return sum.i[h*3];
}

/// <summary>
/// </summary>
/// <param name=""></param>
short* CVIEngineThread::GetSumPtrI(int nSum, bool bModeB)
{
    SUM_IMG &sum = m_pBase->m_summ[nSum];
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    if(!bModeB) return sum.si[0];
    return sum.si[h];
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultVIDraw(int n, DWORD*    imgRes, int rMode)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int cnt = (m_yE-m_yS)*w;

    int res = rMode, id_vi = res;

    DWORD * pRes    = imgRes + m_yS*w, *pResE = imgRes + m_yE*w;
    if(!imgRes)
        return;

    int nSum = m_pBase->m_summ.size();
    if( nSum <= n || n < 0)
        return;

    SUM_IMG &sum = m_pBase->m_summ[n];
    short * imgSum;

    if(IsModeA(res))
        imgSum = sum.si[0];
    else
        imgSum = sum.si[h];

    USHORT *pSum = (USHORT *)(imgSum + m_yS*w);

    DWORD pal[256];
    CopyMemory(pal,(DWORD*)m_pBase->m_palI,sizeof(pal));
    pal[0] = (m_pBase->m_cfg.GetI1(VI_MODE_WBG)&id_vi)?0xFFFFFF:0;

    while(pRes != pResE)
    {
        if(*pSum)
            *pRes = pal[ *pSum ];

        ++pSum;
        ++pRes;
    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultAuraDraw(int n, DWORD*  imgRes, int rMode)
{
    if( (m_pCfg->GetI1(VI_MODE_AURA) & rMode) == 0 )
        return;
    bool bModeB = IsModeB( rMode );
    AURA_STAT & stat = bModeB ? m_pBase->m_statRelease[n].auraB
    : m_pBase->m_statRelease[n].auraA;


    int w,h,y;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);

    if(stat.line.size() != h)
        return;

    int xc = stat.imgCenterX;
    if(xc < 0 || xc >= w)
        return;

    DWORD *pal = (DWORD*)m_pBase->m_palI;

    for( y = m_yS; y < m_yE; ++y )
    {
        DWORD *py = imgRes + y*w;
        AURA_STAT_LINE &l = stat.line[y];
        ////////////////////////////////
        // left
        ////////////////////////////////
        if(l.xla >= 0 && l.cl )
        {
            int cw = round(l.cwl*stat.transform);
            int c = (l.aColorL);
            if(!c)
                continue;
            int xr = l.xla;
            int xl = xr - cw;
            if(xl < 0) xl = 0;
            DWORD crgb = pal[c];

            for(int x = xl; x <= xr; ++x)
            {
                if(x >= 0 && x < w)
                    py[x] = crgb;
            }
        }
        ////////////////////////////////
        // right
        ////////////////////////////////
        if(l.xra >= 0 && l.cr )
        {
            int cw = round(l.cwr*stat.transform);
            int c = (l.aColorR);
            if(!c)
                continue;
            int xl = l.xra;
            int xr = xl + cw;
            if(xr >= w) xr = w-1;
            DWORD crgb = pal[c];

            for(int x = xl; x <= xr; ++x)
            {
                if(x >= 0 && x < w)
                    py[x] = crgb;
            }
        }

    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeResultAuraDrawMix(int n, DWORD*   imgRes, int rMode)
{
    if( (m_pCfg->GetI1(VI_MODE_AURA) & rMode) == 0 )
        return;
    bool bModeB = IsModeB(rMode);
    AURA_STAT & stat = bModeB ? m_pBase->m_statRelease[n].auraB
    : m_pBase->m_statRelease[n].auraA;


    int w,h,y;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);

    if(stat.line.size() != h)
        return;

    int xc = stat.imgCenterX;
    if(xc < 0 || xc >= w)
        return;

    DWORD *pal = (DWORD*)m_pBase->m_palI;
    DWORD cr,cg,cb,ar,ag,ab;

    for( y = m_yS; y < m_yE; ++y )
    {
        DWORD *py = imgRes + y*w;
        AURA_STAT_LINE &l = stat.line[y];
        ////////////////////////////////
        // left
        ////////////////////////////////
        if(l.xla >= 0 && l.cl )
        {
            int cw = round(l.cwl*stat.transform);
            int c = (l.aColorL);

            if(!c)
                continue;

            int xr = l.xla;
            int xl = xr - cw;
            if(xl < 0) xl = 0;
            RGBQUAD *prgb = (RGBQUAD*)(pal+c);

            for(int x = xl; x <= xr; ++x)
            {
                if(x >= 0 && x < w)
                {
                    RGBQUAD *p = (RGBQUAD*)(py+x);
                    cr = prgb->rgbRed;
                    cg = prgb->rgbGreen;
                    cb = prgb->rgbBlue;

                    ar = p->rgbRed;
                    ag = p->rgbGreen;
                    ab = p->rgbBlue;

                    p->rgbRed = (BYTE)((cr+ar)>>1);
                    p->rgbGreen = (BYTE)((cg+ag)>>1);
                    p->rgbBlue = (BYTE)((cb+ab)>>1);
                }
            }
        }
        ////////////////////////////////
        // right
        ////////////////////////////////
        if(l.xra >= 0 && l.cr )
        {
            int cw = round(l.cwr*stat.transform);
            int c = (l.aColorR);
            if(!c)
                continue;
            int xl = l.xra;
            int xr = xl + cw;
            if(xr >= w) xr = w-1;
            RGBQUAD *prgb = (RGBQUAD*)(pal+c);

            for(int x = xl; x <= xr; ++x)
            {
                if(x >= 0 && x < w)
                {
                    RGBQUAD *p = (RGBQUAD*)(py+x);
                    cr = prgb->rgbRed;
                    cg = prgb->rgbGreen;
                    cb = prgb->rgbBlue;

                    ar = p->rgbRed;
                    ag = p->rgbGreen;
                    ab = p->rgbBlue;

                    p->rgbRed = (BYTE)((cr+ar)>>1);
                    p->rgbGreen = (BYTE)((cg+ag)>>1);
                    p->rgbBlue = (BYTE)((cb+ab)>>1);
                }
            }
        }

    }
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::OnEventSumStat(void)
{
    int nSumm = m_pBase->m_summ.size();
    int k;

    SUMM_PTR*   pSumm=0;
    if(nSumm)
    {
        pSumm = new SUMM_PTR[nSumm];
        for( k = 0; k < nSumm; ++k )
        {
            BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
            if(disableSum)
                continue;

            MakeSummPtr(k,pSumm+k);
        }
    }

    for( k = 0; k < nSumm; ++k )
    {
        BOOL disableSum = m_pCfg->GetI1(VI_FILTER_DISABLE_VI0+k);
        if(disableSum)
            continue;

        SUMM_PTR& S = pSumm[k];
        MakeSumStat(k,S);
    }

    _mm_empty();

    delete pSumm;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeSumStat(int nSum, SUMM_PTR& S)
{
    TMP_STAT& T = m_stat[nSum];

    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);

    float *pA = S.pRetA + m_yS*w;
    float *pB = S.pRetB + m_yS*w;
    float *pAE = S.pRetA + m_yE*w;
    float *pBE = S.pRetB + m_yE*w;

    CPointSSE v,sum,cnt,v1(1.0f);
    ///////////////////////////////////////////////////
    // make A
    ///////////////////////////////////////////////////
    sum.set0();
    cnt.set0();
    while( pA != pAE )
    {
        v = pA;
        sum += v;
        v.mask_nz(v1);
        cnt += v;
        pA += 4;
    }
    T.sumAi = sum.sum();
    T.cntAi = cnt.sum();
    ///////////////////////////////////////////////////

    ///////////////////////////////////////////////////
    // make B
    ///////////////////////////////////////////////////
    sum.set0();
    cnt.set0();
    while( pB != pBE )
    {
        v = pB;
        sum += v;
        v.mask_nz(v1);
        cnt += v;
        pB += 4;
    }
    T.sumBi = sum.sum();
    T.cntAi = cnt.sum();
    ///////////////////////////////////////////////////

}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeAuraStatHist(AURA_STAT &stat)
{
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeAuraStatTransform(AURA_STAT &stat)
{
    if( !stat.sline.lcntL || !stat.sline.lcntR )
    {
        stat.transform = 0;
        return;
    }

    float fMax = max( stat.sline.maxL,stat.sline.maxR );
    float xl = ((float)stat.sline.xl)/((float)stat.sline.lcntL);
    float xr = ((float)stat.sline.xr)/((float)stat.sline.lcntR);

    float dx = xr - xl;

    stat.transform = dx / fMax;
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineThread::MakeAuraColorA(int nSum, int x, int y)
{
    int n0 = m_pBase->m_arrDelta.front().n;
    int n = m_pBase->m_cfg.GetI1(m_pBase->m_summ[nSum].id);
    int nLast = n0 - n+1;
    ilFRAME_IMG iDelta;

    if(nLast < 0) nLast = 0;

    float dl = 0;
    int dn=0,cnt=0;

    for(iDelta = m_pBase->m_arrDelta.begin();
        iDelta != m_pBase->m_arrDelta.end(); ++iDelta)
        {
            FRAME_IMG & delta = *iDelta;
            float* py = delta.i[y];
            float v = py[x];
            float dv = fabs(v-dl);

            if(dv > 1.0f)
                ++dn;
            dl = v;

            ++cnt;
            if(delta.n == nLast)
                break;
        }
        if(!cnt)
            return 0;

        return dn*255/cnt;
}

/// <summary>
/// </summary>
/// <param name=""></param>
int CVIEngineThread::res2n(int res)
{
    switch(res)
    {
        default:
        case VI_RESULT_VI0_A:
        case VI_RESULT_VI0_B:
            return 0;

        case VI_RESULT_VI1_A:
        case VI_RESULT_VI1_B:
            return 1;

        case VI_RESULT_VI2_A:
        case VI_RESULT_VI2_B:
            return 2;
    }
    return 0;
}


/// <summary>
/// </summary>
/// <param name=""></param>
bool CVIEngineThread::IsModeA(int res)
{
    switch(res)
    {
        default:
        case VI_RESULT_SRC_A:
        case VI_RESULT_DELTA_A:
        case VI_RESULT_DELTA_FA:
        case VI_RESULT_VI0_A:
        case VI_RESULT_VI2_A:
        case VI_RESULT_VI1_A:
            return true;

        case VI_RESULT_SRC_B:
        case VI_RESULT_DELTA_B:
        case VI_RESULT_DELTA_FB:
        case VI_RESULT_VI0_B:
        case VI_RESULT_VI1_B:
        case VI_RESULT_VI2_B:
            return false;
    }
    return false;
}

/// <summary>
/// </summary>
/// <param name=""></param>
void CVIEngineThread::MakeIntResult(float * pf, short * pi,
                                    mmx_array<float>& hX, mmx_array<float>& hY, int w, int h)
{
    CPointSSE v255(255.0f);
    CPointSSE v,vx,vy;

    hX.resize(w,true);
    hY.resize(h,false);


    if(m_tmp.w != w || m_tmp.h != h)
        m_tmp.resize(w,h,false);

    float *psy = m_tmp.p;

    for(int y = 0; y < h; ++y)
    {
        float *phx = hX.p;
        vy.set0();

        for(int x = 0; x < w; x+=4)
        {
            v = pf;
            v.limit_hi(v255);

            vx = phx;
            vx += v;
            vx.export2c(phx);

            vy += v;

            __m64 i = _mm_cvtps_pi16(v.p);
            (*(ULONGLONG*)pi) = i.m64_u64;

            pf += 4;
            pi += 4;
            phx += 4;
        }

        vy.export2c(psy + (y<<2));
    }

    _mm_empty();

    {
        float * pty =  psy;
        float * phy = hY.begin();;
        float * pe = hY.end();
        while(phy != pe)
        {
            v = pty;
            *phy = v.sum();
            ++phy;
            pty += 4;
        }
    }

    _mm_empty();
}
