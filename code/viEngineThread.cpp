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
/// Функция, адрес которой передайтся при создании паралельной нити в Windows
/// Единственное её действие - передать управление на цикл обработки флагов событий, задаваемых для данного инстанса вычеслений.
/// То есть вызвать функцию CVIEngineThread::Main()
/// </summary>
/// <param name="lpParameter">
/// Указатель на созданный инстанс класса CVIEngineThread к которому собственно и относится эта параллельная нить
/// </param>
DWORD CVIEngineThread::ThreadProc(LPVOID lpParameter)
{
    CVIEngineThread *pThis = (CVIEngineThread*)lpParameter;
    pThis->Main();
    return 0;
}

/// <summary>
/// Обработка взведённых флагов сигналов в цикле пока не скажут - Хватит! - m_events[EVI_DONE].Set()
/// То есть используется сигнально-событийная схема работы программы.
/// Для каждого сигнала предусмотрен свой обработчик.
/// По окончании обработки каждого сигнала взводится флаг m_evReady.
/// </summary>
void CVIEngineThread::Main()
{
    Init();

    SEQ_XOR_SYNC(m_pBase);

    bool bDone = false;

    while(!bDone)
    {
		// WaitForMultipleObjects function
		// Waits until one or all of the specified objects are in the signaled state or the time - out interval elapses.
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
/// Процедура создания параллельной нити в Windows
/// </summary>
bool CVIEngineThread::Start(void)
{
    m_hThread = CreateThread(0,1024*1024,ThreadProc,this,0,&m_nId);
    return (m_hThread != 0);
}

/// <summary>
/// Процедура завершения ранее созданной параллельной нити в Windows
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
/// Расчёт дельта-изображения на основе первых двух кадров в очереди исходных изображений.
/// Расчёт производится одновременно несколькими инстасами данного класса (потоками вычислений).
/// Если значение настроечного параметра VI_VAR_NFRAME равно нулю, то дельта-изображение заполняется нулями и процедура завергается
/// Дельта рассчитывается как абсолютное значение разности между уровнями яркости двух кадров, верхнее и нижнее поля в 2 пикселя полного изображения не расчитываются.
/// Если установлен настроечный параметр VI_FILTER_DELTA_STRETCH, то дельта-сигнал преобразуется по формуле f(x) = (x - filterDeltaLO) * 255.0f/(255.0f - filterDeltaLO);
/// иначе дельта-сигнал уровня яркости меньше filterDeltaLO считается шумом и подавляется (считается минимальной яркостью)
/// и если указано ненулевое значение настроечного параметра VI_FILTER_CT, то применяется неизвестный фильтр подавления шума CVIEngineSimple::FilterCT
/// где filterDeltaLO - значение настроечного параметра VI_FILTER_DELTA_LO
/// Оба указанных из способов являются одними из множества способов подавления шумов.
/// Рассчитывается dsumAsum = сумма значений яркости всех пикселей в дельта-изображении (имеется вводу только обрабатываемый фрагмент изображения) после применения вышеуказанных фильтров подавления шумов.
/// Рассчитывается dsumBsum = количество пикселей с ненулевыми значениями яркости в дельта-изображении (имеется вводу только обрабатываемый фрагмент изображения) после применения вышеуказанных фильтров подавления шумов.
/// Значения dsumAsum и dsumВsum записываются во все элементы массива статистики m_stat.
/// Я же говорил, что лишние теоретические изыски при расчёте дельта-сигнала особо не нужны.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// Количество элементов в строке исходного массива должно быть кратно 4-м.
/// Используются возможности аппаратного ускорения вычислений с применением видеокарты поддерживающей SSE
/// Каждый пискель в видеопамяти состоит из 4-х регистров RGBA,
/// которые могут записыватся и считыватся одновременно и как целые числа и как числа с плавающей точкой.
/// Благодаря этой возможности видеопамяти становится возможно аппаратное ускорение вычислений при преобразовании типов данных из float в int и обратно.
/// То есть достаточно записать в регистры видеопамяти 4 числа с плавающей точкой, а потом считать эти же 4 числа но как целые.
/// Технология SSE уже является устаревшей. Современные видеокарты NVIDIA поддерживают технологию CUDA, позволяющей создавать полноценные программы для параллельных вычислений на видеокартах.
/// Однако стоимость вычислений и возможность параллельных вычислений не всегда находятся в прямой зависимости друкг от друга.
/// Дорогой процессор будет всегда делать вычисления быстрее дешёвой видеокарты, сколько бы там шредеров не было - чудес не бывает.
/// </summary>
void CVIEngineThread::OnEventDelta(void)
{
    ///////////////////////////////////////////////////////////////
    // установка исходных значений
    ///////////////////////////////////////////////////////////////
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h); // Чтение из настроечного параметра VI_VAR_SIZE размеров изображения

    ilFRAME_IMG iSrc = m_pBase->m_arrSrc.begin(); // Получение указателя на первое изображение в последовательности кадров
    if(m_pBase->m_arrSrc.size() <= 1) // Проверка что очередь содержит более одного кадра
        return;
    FRAME_IMG&  imgSrcF = *iSrc; // Получение ссылки на первое изображение в последовательности кадров
    FRAME_IMG&  imgPrvSrcF = *(++iSrc); // Получение ссылки на второе (предыдущее по времени) изображение в последовательности кадров

    // Определение диапазона обрабатываемых строк изображения
	int yS = m_yS,yE = m_yE;
    if(!yS) yS += 2;
    if(yE == h) yE -= 2;

    int cnt = (yE-yS)*w; // Вычисление количества обрабатываемых пикселей

    float * pSrcF = imgSrcF.i[0] + yS*w; // Получение указателя на первый элемент обрабатываемых строк первого изображения в последовательности кадров
    float * pSrcFE = imgSrcF.i[0] + yE*w; // Получение указателя на последний элемент обрабатываемых строк первого изображения в последовательности кадров

    float * pPrvSrcF = imgPrvSrcF.i[0] + yS*w; // Получение указателя на первый элемент обрабатываемых строк второго изображения в последовательности кадров

    ilFRAME_IMG iDelta = m_pBase->m_arrDelta.begin(); // Получение указателя на первое дельт-изображение в последовательности рассчитываемых дельт-изображений
    FRAME_IMG&  imgDeltaF = *iDelta; // Получение ссылки на первое дельт-изображение в последовательности рассчитываемых дельт-изображений

    float * pDeltaA = imgDeltaF.i[0] + yS*w; // Получение указателя на первый элемент обрабатываемых строк первого дельт-изображения в последовательности дельт-кадров

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

                dsumA +=delta;      // интегральная - сумма значений яркости всех пикселей в дельта-изображении
                delta.export2c(pDeltaA);    // сохраняю дельту A

                delta.mask_nz(v1);      // deltaB = (deltaA != 0) ? 1 : 0;

            dsumB += delta;     // интегральная - количество пикселей в дельта-изображении с ненулевым уровнем яркости

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
/// Копирование изображения (имеется вводу только обрабатываемый данным инстансом фрагмент изображения) 
/// из начиная с первого элемента первой строки члена m_arrSrc единственного инстанса родительского класса m_pBase 
/// в начиная с первого элемента первой строки члена m_imgSrcF инстанса данного класса в количестве ширина*высота.
/// Расчёт производится одновременно несколькими инстасами данного класса (потоками вычислений).
/// То есть просто передача данных для дальнейших расчётов в параллельный поток обработки.
/// Хотя для Windows это абсолютно не нужно - можно работать с общей памятью и не дублировать данные для отдельных процессов.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
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
/// Если кратко, то это реализация следующей процедуры с использованием технологии SSE
/// int nSumm = m_pBase->m_summ.size();
/// SUMM_PTR* pSumm = new SUMM_PTR[nSumm];
/// для каждого элемента массива pSumm:
/// MakeSummPtr(k, pSumm + k); 
/// if(!disableA) CRetA = lo[(CSummA + DeltaA - CDeltaA) * divA, fth]
/// if(!disableB) CRetB = lo[(CSummB + 1[DeltaA] - 1[CDeltaA]) * divB, fth]
/// delete pSumm;
/// где указанные переменные являются массивами на которые указывают члены структуры SUMM_PTR,
/// divA и divB - числа указанные в структуре SUMM_PTR,
/// lo[] - применение фильтра подавления слабых сигналов с пороговым значением fth,
/// 1[] - замена ненулевых значений на 1.0
/// и fth = значение настроечного настроечного параметра VI_VAR_TH
/// disableA = значение настроечного настроечного параметра VI_FILTER_DISABLE_A
/// disableB = значение настроечного настроечного параметра VI_FILTER_DISABLE_B
/// Первая формула похожа на расчёт яркости заливки рисунка
/// Вторая формула похожа на расчёт яркости контура рисунка
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// Количество элементов в строке исходного массива должно быть кратно 4-м.
/// Используются возможности аппаратного ускорения вычислений с применением видеокарты поддерживающей SSE
/// Каждый пискель в видеопамяти состоит из 4-х регистров RGBA,
/// которые могут записыватся и считыватся одновременно и как целые числа и как числа с плавающей точкой.
/// Благодаря этой возможности видеопамяти становится возможно аппаратное ускорение вычислений при преобразовании типов данных из float в int и обратно.
/// То есть достаточно записать в регистры видеопамяти 4 числа с плавающей точкой, а потом считать эти же 4 числа но как целые.
/// Технология SSE уже является устаревшей. Современные видеокарты NVIDIA поддерживают технологию CUDA, позволяющей создавать полноценные программы для параллельных вычислений на видеокартах.
/// Однако стоимость вычислений и возможность параллельных вычислений не всегда находятся в прямой зависимости друкг от друга.
/// Дорогой процессор будет всегда делать вычисления быстрее дешёвой видеокарты, сколько бы там шредеров не было - чудес не бывает.
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
/// Последовательное выполнение различных процедур в соответствии со взведёнными двоичными флагами 
/// у значения настроечного параметра VI_MODE_RESULT
/// Список обрабатываемых флагов (согласно очерёдности обработки):
///		VI_RESULT_SRC_0,
///		VI_RESULT_SRC_A,
///		VI_RESULT_SRC_B,
///		VI_RESULT_VI0_A,
///		VI_RESULT_VI0_B,
///		VI_RESULT_VI1_A,
///		VI_RESULT_VI1_B,
///		VI_RESULT_VI2_A,
///		VI_RESULT_VI2_B,
///		VI_RESULT_DELTA_A,
///		VI_RESULT_DELTA_A,
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
/// Инициализация n-того элемента массива структуры SUMM_PTR начальными значениями
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// </summary>
/// <param name="n">Индекс элемента в массиве</param>
/// <param name="pSumm">Указатель на структуру (указатель на n-тый элемент ранее созданного массива)</param>
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
/// Предоставленная реализация функции только устанавливает размер массива m_stat
/// равным размеру массива m_stat родительского инстанса класса реализованных алгоритмов программы.
/// m_stat.resize(m_pBase->m_stat.size());
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
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
/// Предоставленная реализация функции делает ничего
/// </summary>
void CVIEngineThread::ClearStat(void)
{
    for(UINT k = 0; k < m_stat.size(); ++k)
    {
    }
}

/// <summary>
/// Формирование изображения для отображения.
/// Тип формируемого изображения - цветного или монохромного - определяется настроечным параметром VI_MODE_COLOR и наличием исходного цветного изображения.
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// </summary>
/// <param name="res"></param>
void CVIEngineThread::MakeResultSrc(int res)
{
    if(m_pCfg->GetI1(VI_MODE_COLOR) && !m_pBase->m_imgSrc24.empty())
        MakeResultSrc24(res);
    else
        MakeResultSrc8(res);
}


/// <summary>
/// Формирование цветного изображения для отображения.
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// </summary>
/// <param name="res"></param>
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
/// Формирование монохромного изображения для отображения.
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// </summary>
/// <param name="res"></param>
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
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// </summary>
/// <param name="res"></param>
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
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
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
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
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
/// Фильтр устранения шумов изображения (вариант SP)
/// Для каждого пикселя исхоного изображения проверяются яркость 4-х точек - слева, справа, сверх, снизу.
/// Если две и более из них имеют значения яркость меньше 1.0, то в результирующее изображение записывается пиксель нулевой яркости.
/// Иначе копируем в результирующее изображение текущее значение яркости данного пикселя.
/// Поля в один пиксель не обрабатываются.
/// Результат возвращается в том же массиве где были исходны данные.
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name="p">Указатель на список значений яркостей монохромного изображения</param>
void CVIEngineThread::FilterSP(float *p)
{
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    int x,y,w1=w-1,h1=h-1;

    if(m_tmp.w != w || m_tmp.h != h)
        m_tmp.resize(w,h,false);

    SSEmemcpy( m_tmp.p,p,w*h*sizeof(float) ); // Копируем исходные данные во временный массив класса


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
/// Фильтр устранения шумов изображения (вариант SP с применением технологии SSE)
/// Спецификация фильтра аналогична FilterSP.
/// Используется технология SSE для ускорения вычислений.
/// Для каждого пикселя исхоного изображения проверяются яркость 4-х точек - слева, справа, сверх, снизу.
/// Если две и более из них имеют значения яркость меньше 1.0, то в результирующее изображение записывается пиксель нулевой яркости.
/// Иначе копируем в результирующее изображение текущее значение яркости данного пикселя.
/// Поля в один пиксель не обрабатываются.
/// Результат возвращается в том же массиве где были исходны данные.
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name="p"></param>
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
/// Просто цикл по вызову метода MakeAuraV6 для всех значений массива m_nSum
/// Используются настроечные параметры VI_FILTER_DISABLE_A и VI_FILTER_DISABLE_B,
/// а так же настроечные параметры вида VI_FILTER_DISABLE_VI0+k
/// для определения необходимости запускать метод MakeAuraV6 и запускать ли его в режимах A и B
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
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
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name=""></param>
/// <param name="bModeB">Признак формирования результата для режима B</param>
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
/// Подготовка данных для визуализации работы методики.
/// Исходные данные:
/// m_tmp_aura.statHist;    // гистограмма цветов
/// m_tmp_aura.statHistA; // гистограмма цветов по кадру
/// m_tmp_aura.statHistW;  // гистограмма цветов*ширину
/// m_tmp_aura.statHistC;  // гистограмма цветов в контуре
/// m_tmp_aura.iLine;         // округленный цвет
/// Выходные данные:
/// Для режима A результат в массив m_pBase->m_stat[nSum].auraA.line;
/// Для режима B результат в массив m_pBase->m_stat[nSum].auraB.line;
/// Размер результирующего массива равен высоте изображения.
/// Реализация расчётов линий для рисования скрыта в отсутствуюших файлах.
/// Шаги алгоритма:
/// 1-й проход:
/// Определение центра линии - видимо просто берётся средневзвешенная позиция по яркости в линии;
/// Определение цвета линии - видимо просто берётся средневзвешенная по гистограммам цветов всего изображения и гистограмма цветов в линии;
/// Определение длины линии - видимо просто берётся длина линии умноженная на коэффициент корреляции гистограммам цветов всего изображения и гистограмма цветов в линии;
/// 2-й проход:
/// Длина линии разделяется между левой и правой половинами изображения относительно своего центра - похоже разделение происходит по правилу пифагоровых штанов.
/// Как виаулизировать на самом деле не относится к цели методики как я понимаю - здесь главное чтобы было красиво и внушительно.
/// Целью методики как я понимаю было создание полиграфа.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name="nSum">Индекс элемента в массиве m_pBase->m_stat</param>
/// <param name="bModeB">Признак формирования результата для режима B</param>
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
/// Получение указателя на массив 4-х байтных чисел с плавающей точкой, являющижся уровнями яркости пикселей
/// Для режима A return m_pBase->m_summ[nSum].i[h*2];
/// Для режима B return m_pBase->m_summ[nSum].i[h*3];
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name="nSum"></param>
/// <param name="bModeB">Признак формирования результата для режима B</param>
float* CVIEngineThread::GetSumPtr(int nSum, bool bModeB)
{
    SUM_IMG &sum = m_pBase->m_summ[nSum];
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    if(!bModeB) return sum.i[h*2];
    return sum.i[h*3];
}

/// <summary>
/// Получение указателя на массив целых 4-х байтных чисел, являющижся уровнями яркости пикселей
/// Для режима A return m_pBase->m_summ[nSum].si[0];
/// Для режима B return m_pBase->m_summ[nSum].si[h];
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name="nSum"></param>
/// <param name="bModeB">Признак формирования результата для режима B</param>
short* CVIEngineThread::GetSumPtrI(int nSum, bool bModeB)
{
    SUM_IMG &sum = m_pBase->m_summ[nSum];
    int w,h;
    m_pCfg->GetI(VI_VAR_SIZE,w,h);
    if(!bModeB) return sum.si[0];
    return sum.si[h];
}

/// <summary>
/// Копирование изображения в 4-х байтном целочисленном представлении по указанному адресу с заменой цветов на 4-х байтные цвета из палитры.
/// Исходные данные:
/// Для режима A исходное 4-х байтное целочисленное представление изображения берётся по адресу m_pBase->m_summ[n].si[0]
/// Для режима B исходное 4-х байтное целочисленное представление изображения берётся по адресу m_pBase->m_summ[n].si[h]
/// Палитра берётся по адресу m_pBase->m_palI.
/// Размер палитры 256 цветов.
/// Бэк формируется белым, если значение настроечного параметра VI_MODE_WBG битово пересекается с rMode.
/// Размер изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// </summary>
/// <param name="n"></param>
/// <param name="imgRes">Указатель на изображение, где каждый пиксель предствлет 4-мя байтами</param>
/// <param name="rMode"></param>
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

    DWORD pal[256]; // Палитра цветов
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
/// Рисование ауры поверх изображения по указанному адресу с заменой цветов ауры на 4-х байтные цвета из палитры.
/// Исходные данные:
/// Для режима A исходные списки левых и правых линий для рисования берутся из структуры AURA_STAT по адресу m_pBase->m_statRelease[n].auraA
/// Для режима B исходные списки левых и правых линий для рисования берутся из структуры AURA_STAT по адресу m_pBase->m_statRelease[n].auraB
/// Палитра берётся по адресу m_pBase->m_palI. Формат данных в палитре не типизирован - происходит просто копирование 4-х байт при присвоении значения.
/// Размер палитры 256 цветов.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
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
/// Рисование ауры поверх изображения по указанному адресу со смешиванием цветов RGB ауры с исходным RGB изображением.
/// Цвета ауры заменяются на 4-х байтные цвета из палитры в формате RGBQUAD.
/// Исходные данные:
/// Для режима A исходные списки левых и правых линий для рисования берутся из структуры AURA_STAT по адресу m_pBase->m_statRelease[n].auraA
/// Для режима B исходные списки левых и правых линий для рисования берутся из структуры AURA_STAT по адресу m_pBase->m_statRelease[n].auraB
/// Палитра берётся по адресу m_pBase->m_palI и полагается что она представлена в формате RGBQUAD.
/// Размер палитры 256 цветов.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
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
/// Если кратко, то это реализация следующей процедуры с использованием технологии SSE
/// m_stat[nSum].sumAi = CУММА S.RetA ;
/// m_stat[nSum].cntAi = 1[ СУММА S.RetA ];
/// m_stat[nSum].sumBi = CУММА S.RetB ;
/// m_stat[nSum].cntAi = 1[ СУММА S.RetB ];  Описка программиста или так задумано?
/// где 1[] - замена ненулевых значений на 1.0
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Диапазон обрабатываемых строк инстансом данного класса задаётся членами класса m_yS,m_yE.
/// Количество элементов в строке исходного массива должно быть кратно 4-м.
/// Используются возможности аппаратного ускорения вычислений с применением видеокарты поддерживающей SSE
/// Каждый пискель в видеопамяти состоит из 4-х регистров RGBA,
/// которые могут записыватся и считыватся одновременно и как целые числа и как числа с плавающей точкой.
/// Благодаря этой возможности видеопамяти становится возможно аппаратное ускорение вычислений при преобразовании типов данных из float в int и обратно.
/// То есть достаточно записать в регистры видеопамяти 4 числа с плавающей точкой, а потом считать эти же 4 числа но как целые.
/// Технология SSE уже является устаревшей. Современные видеокарты NVIDIA поддерживают технологию CUDA, позволяющей создавать полноценные программы для параллельных вычислений на видеокартах.
/// Однако стоимость вычислений и возможность параллельных вычислений не всегда находятся в прямой зависимости друкг от друга.
/// Дорогой процессор будет всегда делать вычисления быстрее дешёвой видеокарты, сколько бы там шредеров не было - чудес не бывает.
/// </summary>
/// <param name="nSum"></param>
/// <param name="S"></param>
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
    T.cntAi = cnt.sum(); // Описка программиста или так задумано?
    ///////////////////////////////////////////////////

}

/// <summary>
/// Метод не имеет никакого программного кода
/// </summary>
/// <param name="stat"></param>
void CVIEngineThread::MakeAuraStatHist(AURA_STAT &stat)
{
}

/// <summary>
/// </summary>
/// <param name="stat"></param>
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
/// Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res
/// </summary>
/// <param name="res"></param>
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
/// Вычисление признака работы в режиме A согласно списку допустимых значений переменной res
/// </summary>
/// <param name="res"></param>
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
/// Массовая операция конвертации элементов друхмерного массива 4-х байтных чисел с плавающей точкой
/// в двухмерный массив целых 4-х байтных чисел [0;255] с округлением,
/// и одновременным подсчётом сумм элементов по строкам и сумм элементов по столбцам исходного массива.
/// Количество элементов в строке исходного массива должно быть кратно 4-м.
/// Используются возможности аппаратного ускорения вычислений с применением видеокарты поддерживающей SSE
/// Каждый пискель в видеопамяти состоит из 4-х регистров RGBA,
/// которые могут записыватся и считыватся одновременно и как целые числа и как числа с плавающей точкой.
/// Благодаря этой возможности видеопамяти становится возможно аппаратное ускорение вычислений при преобразовании типов данных из float в int и обратно.
/// То есть достаточно записать в регистры видеопамяти 4 числа с плавающей точкой, а потом считать эти же 4 числа но как целые.
/// Технология SSE уже является устаревшей. Современные видеокарты NVIDIA поддерживают технологию CUDA, позволяющей создавать полноценные программы для параллельных вычислений на видеокартах.
/// Однако стоимость вычислений и возможность параллельных вычислений не всегда находятся в прямой зависимости друкг от друга.
/// Дорогой процессор будет всегда делать вычисления быстрее дешёвой видеокарты, сколько бы там шредеров не было - чудес не бывает.
/// </summary>
/// <param name="pf">Указатель на массив чисел с плавающей точкой. Вход.</param>
/// <param name="pi">Указатель на массив целых чисел. Выход.</param>
/// <param name="hX">
/// Сумма элементов по столбцам исходного массива. Выход.
/// Размер массива устанавливается равным переданному параметру ширины изображения.
/// </param>
/// <param name="hY">
/// Сумма элементов по строкам исходного массива. Выход.
/// Размер массива устанавливается равным переданному параметру высоты изображения.
/// </param>
/// <param name="w">Ширина изображения в пикселях</param>
/// <param name="h">Высота изображения в пикселях</param>
void CVIEngineThread::MakeIntResult(float * pf, short * pi,
                                    mmx_array<float>& hX, mmx_array<float>& hY, int w, int h)
{
    CPointSSE v255(255.0f); // Переменная в видеопамяти, состоящая из 4-х регистров.
    CPointSSE v,vx,vy;

    hX.resize(w,true);
    hY.resize(h,false);

	// Изменение размера временного массива под размер текущего обрабатываемого монохромного изображения
    if(m_tmp.w != w || m_tmp.h != h)
        m_tmp.resize(w,h,false);

    float *psy = m_tmp.p; // Получения указателя на элементы временного массива

    for(int y = 0; y < h; ++y) // Цикл по строкам изображения
    {
        float *phx = hX.p; // Получение ссылки на массив элементов с количеством равным ширине изображения
        vy.set0(); // Присвоение переменной значения нуля. Присвоение значения нуля происходит одновременно в 4-х регистрах

        for(int x = 0; x < w; x+=4) // Цикл по элементам строки изображения
        {
            v = pf; // Присвоение текущего значения указателя. Присвоение значений происходит одновременно в 4-х регистрах
            v.limit_hi(v255); // Указание, максимальной пороговой яркости у 4-х регистров видеопамяти при преобразовании к целому, то есть к диапазону [0;255]

			vx = phx; // Присвоение текущего значения по указателю. Присвоение значений происходит одновременно в 4-х регистрах
			vx += v; // Прибавление текущего значения. Прибавление значений происходит одновременно в 4-х регистрах
            vx.export2c(phx); // Сохранение вычисленных знвчений. Сохранение значений происходит одновременно в 4-х регистрах

            vy += v; // Накопление суммы. Суммирование происходит одновременно в 4-х регистрах

			// Нижеследующий блок является одновременной конвертацией сразу 4-х чисел с плавающей точкой в 4 целях числа
			// Как правило, при равных мощностях ЦП и видеокарты, вычисления с плавающей точкой являются более долгой операцией 
			// на процессоре и данная техника позволяет ускорить вычисления с помощью видеокарты

			// The __m64 data type is for use with the MMX and 3DNow! intrinsics, and is defined in xmmintrin.h.
			// You should not access the __m64 fields directly. You can, however, see these types in the debugger. A variable of type __m64 maps to the MM[0-7] registers.
			// Variables of type _m64 are automatically aligned on 8 - byte boundaries.
			// The __m64 data type is not supported on x64 processors.Applications that use __m64 as part of MMX intrinsics must be rewritten to use equivalent SSE and SSE2 intrinsics.
			// http://msdn.microsoft.com/en-us/library/08x3t697.aspx
			// Converts the four single-precision, floating-point values in a to four signed 16-bit integer values.
			// http://msdn.microsoft.com/en-us/library/yk5x06zy(v=vs.90).aspx
            __m64 i = _mm_cvtps_pi16(v.p);
            (*(ULONGLONG*)pi) = i.m64_u64;

            pf += 4;
			pi += 4; 
			phx += 4; // Смещение указателя на 4 элемента == переходу к обработке следующих 4-х элемента строки изображения
        }

        vy.export2c(psy + (y<<2)); // Сохранение накопленной суммы по соответствующему адресу массива m_tmp
    }

	// Empties the multimedia state.
	// http://msdn.microsoft.com/en-us/library/6303a3sf(v=vs.90).aspx
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

	// Empties the multimedia state.
	// http://msdn.microsoft.com/en-us/library/6303a3sf(v=vs.90).aspx
	_mm_empty();
}
