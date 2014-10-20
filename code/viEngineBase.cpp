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
/// Конструктор класса
/// Инициализация переменных программы.
/// Определение оптимального количества параллельных нитей для вычислений.
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

    SEQ_XOR_INIT(this); // Вероятно относится к защите от деассемблирования программы

    m_nThreads = GetOptimalThreadCount(); // Расчёт оптимального количества параллельных процессов для данного компьютера, на котором выполняется программа.
    ZeroMemory(m_therads,sizeof(m_therads));
    MakeDefPal(m_palI);

	SEQ_GUARDANT_INIT(this); // Вероятно относится к защите от деассемблирования программы

    m_tMakeImage = m_timer.Get();  // Нормализованное в секунды значение часов

	SEQ_INET_STEP01A(this); // Вероятно относится к защите от деассемблирования программы
    m_bInit = true;
}

/// <summary>
/// </summary>
CVIEngineBase::~CVIEngineBase(void)
{
    Stop();
}

/// <summary>
/// Запуск параллельных нитей Windows для параллельных вычислений.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="bLock">Признак блокирования семафора LVI_ALL с ожиданием его освобождения другими процессами</param>
void CVIEngineBase::CreateThreads(bool bLock)
{
    CMTSingleLock   lock(m_locks + LVI_ALL, bLock); // Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.
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

    m_hThreadAddImage = CreateThread(0,0,AddImageThread,this,0,0); // Создание параллельной нити с обработчиком AddImageThreadLocal
    m_hThreadAddImage8 = CreateThread(0,0,AddImageThread8,this,0,0); // Создание параллельной нити с обработчиком AddImageThreadLocal8

    SetThreadPriority(m_hThreadAddImage8,THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(m_hThreadAddImage,THREAD_PRIORITY_ABOVE_NORMAL);
}

/// <summary>
/// Останов ранее запущенных параллельных потоков для вычислений.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name=""></param>
/// <param name="bLock">Признак блокирования семафора LVI_ALL с ожиданием его освобождения другими процессами</param>
void CVIEngineBase::CloseThreads(bool bLock)
{
	m_cfg.PutI1(VI_FILTER_PAUSE, 1); // Запись первого значения настроечного параметра по идентификатору ключа

    m_events[EVI_DONE].Set();
    if(m_therads[0] && m_therads[0]->m_hThread)
        Sleep(1000);

    CMTSingleLock   slock(m_locks + LVI_SRC, true); // Блокирование семафора LVI_SRC с ожиданием его освобождения другими процессами.
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

	CMTSingleLock   lock(m_locks + LVI_ALL, bLock); // Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.

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
	m_cfg.PutI1(VI_FILTER_PAUSE, 0); // Запись первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Чтение очередного кадра изображения в аттрибут m_imgSrc8.
/// Конвертация в массив чисел с плавающей точкой.
/// Вызов когда требуется AddImageThreadProc().
/// Процедура содержит задержку для соблюдения требуемой часоты чтения кадров.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
bool CVIEngineBase::AddImage8()
{
    if(!m_cfg.GetI1(VI_VAR_NFRAME_IN)) // Чтение первого значения настроечного параметра по идентификатору ключа
        return false;

	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	DWORD tS = GetTickCount(), tE, tD;  // Время начала работы данного процесса, время его завершения работы, продолжительность его работы

    int w = m_imgSrc8.w;
    int h = m_imgSrc8.h;
    BYTE *p = m_imgSrc8.p;

    SEQ_XOR_SYNC(this);
    if( m_bStop )
        return false;

    float fpsMax = m_cfg.GetF1(VI_VAR_FPSMAXF); // Чтение первого значения настроечного параметра по идентификатору ключа
    BOOL bFpsDiv = (m_cfg.GetI1(VI_FILTER_FPSDIV)); // Чтение первого значения настроечного параметра по идентификатору ключа
    ///////////////////////////////////////////////////////
    // делитель чк
    ///////////////////////////////////////////////////////
    if(bFpsDiv)
    {

        float fpsIn = m_cfg.GetF1(VI_VAR_FPSIN); // Чтение первого значения настроечного параметра по идентификатору ключа
        int div = fpsMax > 0 ? round(fpsIn/fpsMax) : 1;
        if(div > 1  || m_cfg.GetI1(VI_VAR_NFRAME_IN) < 3)
        {
            int N = m_cfg.GetI1(VI_VAR_NFRAME_IN); // Чтение первого значения настроечного параметра по идентификатору ключа
            if(N%div != 0)
            {
                return false;
            }
        }
    }
    ///////////////////////////////////////////////////////

	m_cfg.PutI1(VI_VAR_FPS_BUFFER_SIZE, m_srcF.size()); // Запись первого значения настроечного параметра по идентификатору ключа
    CMTSingleLock   slock(m_locks + LVI_SRC, true);  // Блокирование семафора LVI_SRC с ожиданием его освобождения другими процессами.
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

	CMTSingleLock   lock(m_locks + LVI_SRC8, true);  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.

    CVIEngineSimple::cvt_8i_to_32f(p,img.i.p,size,this);

    lock.Unlock();

    m_stat2.Add(img.i.p,img.i.w,img.i.h);

    float fpsOutF = m_fpsOutF.Put();
    m_cfg.PutF1(VI_VAR_FPSOUTF, min(fpsOutF,m_cfg.GetF1(VI_VAR_FPSIN)) );  // Запись первого значения настроечного параметра по идентификатору ключа

    int nFrame = m_cfg.GetI1(VI_VAR_NFRAME_OUTF); // Чтение первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutI1(VI_VAR_NFRAME_OUTF, nFrame + 1); // Запись первого значения настроечного параметра по идентификатору ключа

    slock.Unlock();

    MakeSin();

    if(bFpsDiv)
        AddImageThreadProc();

    if(!bFpsDiv)
    {
        float fpsCur = m_cfg.GetF1(VI_VAR_FPSOUTF); // Чтение первого значения настроечного параметра по идентификатору ключа

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
/// Восстановление значения настроечного параметра VI_FILTER_FPSDIV значением настроечного параметра VI_FILTER_FPSDIV_RQST.
/// В случае если настроечный параметр VI_FILTER_FPSDIV отличался от настроечного параметра VI_FILTER_FPSDIV_RQST,
/// производится очистка массива m_srcF.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
bool CVIEngineBase::CheckFPSDIV()
{
    if(m_cfg.GetI1(VI_FILTER_FPSDIV) == m_cfg.GetI1(VI_FILTER_FPSDIV_RQST))
        return false;
	CMTSingleLock lock(m_locks + LVI_SRC, true);  // Блокирование семафора LVI_SRC с ожиданием его освобождения другими процессами.

    m_srcF.clear();

	m_cfg.PutI1(VI_FILTER_FPSDIV, m_cfg.GetI1(VI_FILTER_FPSDIV_RQST)); // Запись первого значения настроечного параметра по идентификатору ключа
    return true;
}

/// <summary>
/// Процедура получения очередной порции данных для обработки.
/// Список шагов:
/// Действия по оптимизации частоты обработки кадров.
/// Синхронизация таймера.
/// Подготовки различных внутренних структур к готовности получать данные от устройства захвата звука или изображения.
/// Подготовка масок для обрабатываемых изображений.
/// Вызов интерфейсных функций подключаемых программных модулей для получения кадра изображения в m_imgSrc8 или m_imgSrc24 в зависимости от настроек.
/// Поднятие флагов готовности данных для дальнейшей обработки.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="p"></param>
/// <param name="w">Ширина изображения</param>
/// <param name="h">Высота изображения</param>
/// <param name="bpp"></param>
/// <param name="t">Отметка времени</param>
/// <param name="nRef"></param>
bool CVIEngineBase::AddImage(void* p, int w, int h, int bpp, double t, int nRef)
{
    if(t > m_tVideo)
    {
        double dt = t - m_tVideo;
        if(m_tVideoDT > 0 && fabs( (m_tVideoDT-dt)*2/(m_tVideoDT+dt) ) > 0.5)
			m_cfg.PutI1(VI_VAR_NDROP, m_cfg.GetI1(VI_VAR_NDROP) + 1); // Запись первого значения настроечного параметра по идентификатору ключа

        m_tVideoDT = dt;
    } else
        if( t > 0 && t == m_tVideo )
            return false;

        m_tVideoPrev = m_tVideo;
    m_tVideoTPrev = m_tVideoT;
    m_tVideo = t;
    m_tVideoT = m_timer.Get();  // Нормализованное в секунды значение часов

    if(p && w && h )
    {
        if(!m_timerSync.Add(t) )
            NewSource(); // Подготовки различных внутренних структур к готовности получать данные от устройства захвата звука или изображения.

        m_audio.OnVideo(); // Запуск процедуры для копирования текущего кадра данных от медийного устройства в инстанс класса CVIEngineBase
    } else
        NewSource(); // Подготовки различных внутренних структур к готовности получать данные от устройства захвата звука или изображения.

    if( m_summ.empty() )
    {
        return false;
    }

    switch( m_cfg.GetI1(VI_VAR_CFG_INIT)  ) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
        default:
        case 0:
            return false;
        case 1:
			m_cfg.PutI1(VI_VAR_CFG_INIT, 2); // Запись первого значения настроечного параметра по идентификатору ключа
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
			xlock.Lock();  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.

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
        if( m_cfg.GetI1( VI_FILTER_PAUSE ) ) // Чтение первого значения настроечного параметра по идентификатору ключа
            return false;

        int nFrame = m_cfg.GetI1(VI_VAR_NFRAME_IN); // Чтение первого значения настроечного параметра по идентификатору ключа
		m_cfg.PutI1(VI_VAR_NFRAME_IN, nFrame + 1); // Запись первого значения настроечного параметра по идентификатору ключа

        if(!m_cfg.GetI1(VI_FILTER_FPSDIV)) // Чтение первого значения настроечного параметра по идентификатору ключа
        {
            int dfr = m_cfg.GetI1(VI_FILTER_FPS2IN); // Чтение первого значения настроечного параметра по идентификатору ключа

            if(!dfr && m_cfg.GetI1(VI_FILTER_AUTODOWNRATE) && m_tVideo > 5)
            {
                float fpsMax = max( m_cfg.GetF1(VI_VAR_FPSMAXR), // Чтение первого значения настроечного параметра по идентификатору ключа
                                    m_cfg.GetF1(VI_VAR_FPSMAXF) ); // Чтение первого значения настроечного параметра по идентификатору ключа
                float fpsCur = m_fpsIn.Get();

                if(fpsMax > 0)
					m_cfg.PutI1(VI_FILTER_FPS2IN, ceil(fpsCur / fpsMax)); // Запись первого значения настроечного параметра по идентификатору ключа
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



	xlock.Lock();  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.

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
                    SSEmemcpy(m_imgSrc8.begin(),p,m_imgSrc8.size()); // Копирование содержимого памяти
    }

    if(!m_bLock)
    {
        if(!m_cfg.GetI1(VI_FILTER_FPSDIV)) // Чтение первого значения настроечного параметра по идентификатору ключа
			m_cfg.PutF1(VI_VAR_FPSIN, m_fpsIn.Put()); // Запись первого значения настроечного параметра по идентификатору ключа
        else
			m_cfg.PutF1(VI_VAR_FPSIN, m_fpsIn.PutT(round(t * 1000))); // Запись первого значения настроечного параметра по идентификатору ключа
    }

    if(CallbackOnImg8)
        CallbackOnImg8(CallbackOnImg8Data,m_imgSrc8.begin(),m_imgSrc8.w,m_imgSrc8.h,t);
    xlock.Unlock();

    if(m_cfg.GetI1(VI_FILTER_FPSDIV) && m_cfg.GetI1(VI_VAR_NFRAME_IN) < 3)
        return false;

	CMTSingleLock   xlock24(m_locks + LVI_SRC24, true);  // Блокирование семафора LVI_SRC24 с ожиданием его освобождения другими процессами.
    if( m_cfg.GetI1(VI_MODE_COLOR) ) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
        m_imgSrc24.resize(w,h,false);
        int wh = w*h;
        if( bpp == 24 )
            SSEmemcpy(m_imgSrc24.p,p,wh*3);  // Копирование содержимого памяти
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

        if(m_cfg.GetI1(VI_FILTER_FPSDIV)) // Чтение первого значения настроечного параметра по идентификатору ключа
            m_events[EVI_ADD8_READY].Wait(INFINITE,
                                          m_events[EVI_DONE].m_hEvent);

    }

    int rmode = m_cfg.GetI1(VI_MODE_RESULT);
    if( (rmode & VI_RESULT_SRC_MASK) || m_bLock)
    {
		CMTSingleLock   rlock(m_locks + LVI_RESULT, true);  // Блокирование семафора LVI_RESULT с ожиданием его освобождения другими процессами.
        MakeResultSrc();
    }


    return true;
}

/// <summary>
/// Процедура обработки очередной порции кадров изображения.
/// Собственно и содержит перечисление всех тех шагов, которые проходит обработка изображения - то есть последовательный вызов процедур обработки и расчётов.
/// Часть расчётов осуществляется в параллельных потоках.
/// Чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения, 
/// то просто взводится соответствующий флага сигнала у всех параллельных потоков обработки методом Make.
/// Шаги:
/// Сброс если надо - Reset(true);
/// Сдвиг очереди кадров - NextSrc();
/// Сброс ранее рассчитанных данных - ClearStat();
/// Получение очередного изображения CVIEngineFace::AddImage;
/// Последовательное применение различных обработок кадра - фильтрация шумов, расчёт статистики, расчёт изображения ауры - если по ходу не подали сигнал остановки вычислений;
/// Сведение результатов от фрагментов вместе и расчёт статистических значений;
/// Наконец, вызов методов собственно которые должны реализовавать методику определения чего-то психологического:
///		MakeAnger();
///		MakeStress();
///		MakeState();
/// Вызов процедур подготовки результатов для визуализации;
/// Вызов процедуры визуализации результатов - MakeFaceDraw();
/// Расчёт затраченного времени на обработку и производительности (FPS), формирование номера сдедующего кадра:
///		m_cfg.PutF1(VI_VAR_FPSOUTR, m_fpsOutR.Put());
///		m_cfg.PutI1(VI_VAR_NFRAME, m_cfg.GetI1(VI_VAR_NFRAME) + 1);
/// Конец процедуры;
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="pFI">Указатель на массив пикселей в формате чисел с плавющей точной. Видимо зарезервировано для дайнейшего использования.</param>
/// <param name="pBI">Указатель на массив пикселей в формате целых чисел. Использование зависит от флагов при компилировании программы в исполняемый код.</param>
/// <param name="w">Ширина изображения</param>
/// <param name="h">Высота изображения</param>
bool CVIEngineBase::MakeImage(float* pFI, BYTE *pBI, int w, int h)
{
    if(m_bDone || m_bStop)
        return false;

    int cw,ch;
    m_cfg.GetI(VI_VAR_SIZE,cw,ch);
    if(cw != w || ch != h)
        return false;

    ++m_nMake;
	CMTSingleLock   lock(m_locks + LVI_ALL, true);  // Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.

    if( m_cfg.GetI1(VI_VAR_RESET) )
        Reset(true);


    if(!m_cfg.GetI1(VI_VAR_NFRAME_IN))
        return false;

    m_imgSrcF = pFI;
    if(!m_bStop)
        NextSrc(); // Сдвиг очереди кадров

    if(!m_bStop) // 080227
        ClearStat(); // Сброс, очистка и обнуление ранее вычисленных статистических значений.

    #ifndef SEQ_DISABLE_FACE
    CVIEngineFace::AddImage(this,pBI,w,h); // Получение очередного изображения
    #endif

    if(!m_bStop)
        Make(EVI_ADDF); // Поднятие флага у сигнала, чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения

    if(!m_bStop)
		Make(EVI_DELTA); // Поднятие флага у сигнала, чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения

    if(!m_bStop)
		Make(EVI_SUM); // Поднятие флага у сигнала, чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения

    if(!m_bStop)
		Make(EVI_SUM_FILTER); // Поднятие флага у сигнала, чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения

    #ifndef SEQ_DISABLE_FACE
    CVIEngineFace::Sync(this);
    #endif

    if(!m_bStop)
		Make(EVI_SUM_STAT); // Поднятие флага у сигнала, чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения

    if(!m_bStop)
        MakeStatSum();


    if(!m_bStop && m_cfg.GetI1(VI_VAR_NFRAME) > 3)
        Make(EVI_AURA);  // Поднятие флага у сигнала, чтобы потоки обработки приступили к соответствующей обработке своего фрагмента изображения


    MakeMotion(); /// Вызов метода Reset() если выполняется условие от процедуры сенсора движения.

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

    MakeState(); // Выбор коэффициента либо из настроек, либо рассчитать на основе вычисленных статистик, вызовом одноимённой функции с параметрами, сохранение параметров.

    #ifndef SEQ_DISABLE_FACE
    if(m_pFace)
        m_pFace->MakeRelease();
    #endif
    if(m_resultSize.cx == w && m_resultSize.cy == h && !m_bStop)
    {
        int rmode = m_cfg.GetI1(VI_MODE_RESULT); // Чтение первого значения настроечного параметра по идентификатору ключа
        if( rmode )
        {
			CMTSingleLock   rlock(m_locks + LVI_RESULT, true);  // Блокирование семафора LVI_RESULT с ожиданием его освобождения другими процессами.
            Make(EVI_RESULT);
            MakeFaceDraw(); // Вывод картинки на экран через API в классе CVIEngineFace.
            ++m_resultVer;
        }
    }

    if(!m_bStop)
    {
		m_cfg.PutF1(VI_VAR_FPSOUTR, m_fpsOutR.Put()); // Запись первого значения настроечного параметра по идентификатору ключа
		m_cfg.PutI1(VI_VAR_NFRAME, m_cfg.GetI1(VI_VAR_NFRAME) + 1); // Запись первого значения настроечного параметра по идентификатору ключа
    }

    lock.Unlock();

    --m_nMake;

    m_tMakeImage = m_timer.Get();  // Нормализованное в секунды значение часов
    //////////////////////

    return true;
}

/// <summary>
/// Сохранение новых размеров изображения в настроечных параметрах VI_VAR_SIZE.
/// Изменение размеров массивов используемых для хранения и обработки изображений.
/// Перезапуск потоков обработки изображений.
/// </summary>
/// <param name="w">Ширина изображения</param>
/// <param name="h">Высота изображения</param>
/// <param name="cnt">Новое количество обрабатываемых кадров</param>
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



    ClearStat(); // Сброс, очистка и обнуление ранее вычисленных статистических значений.

    CreateThreads();

    return true;
}

/// <summary>
/// Сброс настроечного параметра VI_VAR_NFRAME.
/// Изменение размера массивов m_arrSrc и m_arrDelta до размера  cnt + 1 и изменение размеров аллокированной под кадры памяти.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="cnt">Новое количество обрабатываемых кадров</param>
void CVIEngineBase::SetCount(int cnt, bool bLock)
{
    Reset();

	CMTSingleLock   lock(m_locks + LVI_ALL, bLock);  // Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.

	m_cfg.PutI1(VI_VAR_NFRAME, 0); // Запись первого значения настроечного параметра по идентификатору ключа

    m_arrSrc.resize(cnt + 1);


    m_arrDelta.resize(cnt + 1);

    ilFRAME_IMG i;
    int pos;

    SIZE size;
    m_cfg.GetI(VI_VAR_SIZE,size.cx,size.cy);

    int nFrame = m_cfg.GetI1(VI_VAR_NFRAME); // Чтение первого значения настроечного параметра по идентификатору ключа

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
/// Сдвиг очереди кадров в памяти
/// </summary>
mmx_array2<float>& CVIEngineBase::NextSrc(void)
{
    ilFRAME_IMG i;

    i = m_arrSrc.end(); --i;
    m_arrSrc.splice(m_arrSrc.begin(),m_arrSrc,i);

    FRAME_IMG& img = m_arrSrc.front();
    img.n = m_cfg.GetI1(VI_VAR_NFRAME); // Чтение первого значения настроечного параметра по идентификатору ключа

    i = m_arrDelta.end();   --i;
    m_arrDelta.splice(m_arrDelta.begin(),m_arrDelta,i);

    FRAME_IMG& delta = m_arrDelta.front();
    delta.n = m_cfg.GetI1(VI_VAR_NFRAME); // Чтение первого значения настроечного параметра по идентификатору ключа

    return img.i;
}

/// <summary>
/// Ожидание готовности всех параллельных потоков обработки изображения.
/// Используется для синхронизации параллельных вычислений.
/// </summary>
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
/// Изменение размеров массивов m_stat, m_summ, m_aura6A, m_aura6B до нового размера.
/// Присвоение m_summ[i].id = pN[i] 
/// и очистка памяти выделеной для хранения строк изображений m_summ[i].i.clear();m_summ[i].si.clear();
/// Инициализация структур ClearStat(m_stat[i]) если азмер массивов был изменён.
/// Для подробностей надо смотреть описание структуры SUM_IMG;
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="cnt">Новый размер массивов</param>
/// <param name="pN">Список значений аттрибута m_summ[i].id</param>
void CVIEngineBase::SetSummCount(int cnt, int *pN)
{
	CMTSingleLock   lock(m_locks + LVI_ALL, true);  // Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.
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
/// Заполнение массива по указателю значениями pN[i] = m_cfg.GetI1(m_summ[i].id);
/// Возвращает размер массива m_summ;
/// Для подробностей надо смотреть описание структуры SUM_IMG;
/// </summary>
/// <param name="pN">Указатель на заполняемый массив значениями pN[i] = m_cfg.GetI1(m_summ[i].id);</param>
int CVIEngineBase::GetSummCount(int *pN)
{
    UINT cnt = m_summ.size();
    if(pN)
    {
        for(UINT i = 0; i  < cnt; ++i)
            pN[i] = m_cfg.GetI1(m_summ[i].id); // Чтение первого значения настроечного параметра по идентификатору ключа
    }
    return cnt;
}

/// <summary>
/// Получение значения настроесного параметра return m_cfg.GetI1(m_summ[pos].id);
/// Для подробностей надо смотреть описание структуры SUM_IMG;
/// </summary>
/// <param name="pos">Индекс элемента в массиве m_summ</param>
int CVIEngineBase::GetSummCount(int pos)
{
    int cnt = m_summ.size();
    if(pos < 0 || pos >= cnt)
        return -1;
    return m_cfg.GetI1(m_summ[pos].id); // Чтение первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Проверяет наличие записи в карте m_resultPtr по ключу id и значению ptr и совпадение размера в m_resultSize с переданными значениями.
/// Если что-то не совпадает или не хватет, то 
/// добавляет в карту m_resultPtr по ключу id значению ptr;
/// задаёт значение размера m_resultSize равным переданным параметрам;
/// Возвращает false если размер изображения не совпадает с хранящимся в настроечном параметре VI_VAR_SIZE;
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="id">Ключ карты m_resultPtr</param>
/// <param name="ptr"></param>
/// <param name="w">Ширина изображения</param>
/// <param name="h">Высота изображения</param>
bool CVIEngineBase::SetResultPtr(int id, void* ptr, int w, int h)
{
    if( w == m_resultSize.cx && h == m_resultSize.cy )
    {
        std::map<int,DWORD*>::iterator i = m_resultPtr.find(id);
        if(i != m_resultPtr.end() && i->second == ptr)
            return true;
    }
	CMTSingleLock   lock(m_locks + LVI_RESULT, true);  // Блокирование семафора LVI_RESULT с ожиданием его освобождения другими процессами.
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
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Если он не задан, то берутся значения размеров из m_resultSize.
/// Возвращает через параметры значение из карты m_resultPtr по ключу id и размеры изображения.
/// Возвращает true, если размеры в VI_VAR_SIZE совпадают со значениями в m_resultSize и существует запись для ключа id в карте m_resultPtr.
/// </summary>
/// <param name="id">Ключ карты m_resultPtr</param>
/// <param name="ptr">Указатель на возвращаемое значение указателя</param>
/// <param name="pw">Указатель на возвращаемое значение ширины</param>
/// <param name="ph">Указатель на возвращаемое значение высоты</param>
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
/// Поднятие флага сигнала command у всех параллельных процессов.
/// m_therads[k]->m_events[command].Set();
/// Параллельные процессы у себя в цикле проверяют поднятость флагов у своего инстанса и выполняют соответствующую сигналу процедуру.
/// </summary>
/// <param name="command">Идентификатор сигнала-события</param>
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
/// Расчёт оптимального количества параллельных процессов для данного компьютера, на котором выполняется программа.
/// Данная реализация возвращает просто количество процессоров в на компьютере, но не более 8-ми и не менее 1-го ;) 
/// То есть простенько - без вяких лишних околонаучных движений.
/// SYSTEM_INFO info; GetSystemInfo(&info); nProc = info.dwNumberOfProcessors;
/// </summary>
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
/// Объединение результатов, рассчитанных в отдельный параллельный процессах для отдельных фрагментов изображений в единый результат в одном месте, то есть для инстанса данного класса и расчёт средних значений по всему изображению.
/// Паралельные процессы сохраняют посчитанные результаты в своих инстансах CVIEngineThread в массивах структур m_stat.
/// Данная процедура объединяет все эти результаты в массиве m_stat и что-то делит на количество пикселей в изображении (суммарную яркость, количество ненулевых пикселей и т.д.) - то есть рассчитывает усреднёные значения.
/// Перед выполнении данной процедуры надо убедится в завершении расчётов в каждом отдельном параллельном потоке, поскольку в данном методе вызовов синхронизации нет - здесь полагается что все потоки вычислений уже закончили свои вычисления по текущему кадру.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
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
        BOOL disableSum = m_cfg.GetI1(VI_FILTER_DISABLE_VI0+i); // Чтение первого значения настроечного параметра по идентификатору ключа

        SUMM_STAT& S = m_stat[i];

        if(disableSum)
        {
            if(!S.bClear)
                ClearStat(S); // Сброс, очистка и обнуление ранее вычисленных статистических значений.
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
/// Сброс, очистка и обнуление ранее вычисленных статистических значений.
/// То есть подготовка данных структуры SUMM_STAT к очередному циклу обработки.
/// </summary>
/// <param name="S">Ссылка на инстанс структуря SUMM_STAT</param>
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
/// Сброс, очистка и обнуление ранее вычисленных статистических значений.
/// То есть подготовка массива m_stat структуры SUMM_STAT к очередному циклу обработки.
/// Процедура вызывает одноимённый метод для каждого элемента массива m_stat.
/// </summary>
void CVIEngineBase::ClearStat(void)
{
    int cnt = m_stat.size();

    for(int i = 0 ; i < cnt; ++i)
    {
        SUMM_STAT& S = m_stat[i];
        ClearStat(S); // Сброс, очистка и обнуление ранее вычисленных статистических значений.
    }
}

/// <summary>
/// Задание идентификатора ключа в реестре Windows для хранения группы настроечных параметров
/// </summary>
/// <param name="group">Идентификатор группы ключей в реестре Windows</param>
void CVIEngineBase::SetRegistry(LPCTSTR group)
{
    m_cfg.SetRegistry(group);
}

/// <summary>
/// Копирование из m_imgSrc8 значений x-ого столца и y-ой строки по указанным значениям указателей.
/// m_imgSrc8 - двумерный массив монохромного изображения [0;255];
/// То есть будет выполнятся преобразования из BYTE в float.
/// </summary>
/// <param name="x">Номер столбца</param>
/// <param name="y">Номер строки</param>
/// <param name="px">Указатель на массив возвращаемых значений столбца</param>
/// <param name="py">Указатель на массив возвращаемых значений строки</param>
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
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
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

    UINT n = res2n(id); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res.
    if(n >= m_stat.size())
        return false;

    SUMM_STAT &S = m_stat[n];
    CVIEngineXHist *phx = 0,*phy = 0;

    if( IsModeA(id) )  // Вычисление признака работы в режиме A согласно списку допустимых значений переменной
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
/// Копирование значений x-ого столца и y-ой строки по указанным значениям указателей.
/// Исходный массив для копирования определяется на основе настроечного параметра VI_MODE_RESULT.
/// VI_RESULT_SRC_0 m_arrSrc.begin()->i.begin();
/// VI_RESULT_SRC_A m_arrSrc.begin()->i.begin();
/// VI_RESULT_SRC_B m_arrSrc.begin()->i.begin();
/// VI_RESULT_VI0_A m_summ[0]->i[h*2];
/// VI_RESULT_VI0_B m_summ[0]->i[h*3];
/// VI_RESULT_VI1_A m_summ[1]->i[h*2];
/// VI_RESULT_VI1_B m_summ[1]->i[h*3];
/// VI_RESULT_VI2_A m_summ[2]->i[h*2];
/// VI_RESULT_VI2_B m_summ[2]->i[h*3];
/// VI_RESULT_DELTA_A m_arrDelta.begin()->i[0];
/// VI_RESULT_DELTA_B m_arrDelta.begin()->i[0];
/// Если m_cfg.GetI1(VI_MODE_RESULT) = VI_RESULT_DELTA_B, то производится умножение на 255.0;
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// Применяется технология SSE для ускорения копирования и преобразования типов.
/// </summary>
/// <param name=""></param>
bool CVIEngineBase::GetSrcLine(int x, int y, float* px, float* py)
{
    if(m_bLock)
        return GetSrcLine8(x,y,px,py);// Копирование из m_imgSrc8 значений x-ого столца и y-ой строки по указанным значениям указателей.

    if(m_arrSrc.empty())
        return false;
    int w,h;
    m_cfg.GetI(VI_VAR_SIZE,w,h);

    if(y < 0 || y >= h || x < 0 || x >= w)
        return false;

    ilFRAME_IMG iSrc = m_arrSrc.begin();

    float *pImg = iSrc->i.begin();

    int mode = m_cfg.GetI1(VI_MODE_RESULT); // Чтение первого значения настроечного параметра по идентификатору ключа
    bool bMul255 = false; // Равен true если m_cfg.GetI1(VI_MODE_RESULT) == VI_RESULT_DELTA_B

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
/// Расчёт величин определённых в методике и записи их в настроечные параметры
/// BOOL disabled2X =m_cfg.GetI1(VI_FILTER_DISABLE_2X);
/// VI_VAR_STAT_INTEGR0A = m_stat[0].sumAin;
/// VI_VAR_STAT_INTEGR0A = m_stat[0].sumBin;
/// VI_VAR_STAT_INTEGR1A = m_stat[1].sumAin;
/// VI_VAR_STAT_INTEGR1A = m_stat[1].sumBin;
/// VI_VAR_STAT_INTEGR2A = m_stat[2].sumAin;
/// VI_VAR_STAT_INTEGR2A = m_stat[2].sumBin;
/// VI_VAR_STAT_RES_A1 = m_stat[0].dsumAin;
/// if(disabled2X) VI_VAR_STAT_RES_A1X = m_stat[0].dsumAin;
/// VI_VAR_STAT_RES_A2 = ( (m_stat[1].auraA.sline.sumL / m_stat[1].auraA.sline.cl) + (m_stat[1].auraA.sline.sumR / m_stat[1].auraA.sline.cr) ) / 2;
/// VI_VAR_STAT_RES_A3 = ( (m_stat[0].auraA.sline.sumL / m_stat[0].auraA.sline.cl) + (m_stat[0].auraA.sline.sumR / m_stat[0].auraA.sline.cr) ) / 2;
/// VI_VAR_STAT_RES_A4 =  m_statAVG.Get(VI_VAR_STAT_RES_A1);
/// if(disabled2X) VI_VAR_STAT_RES_A4X =  m_statAVG.Get(VI_VAR_STAT_RES_A1X) ;
/// VI_VAR_STAT_RES_F1 = m_stat[0].dsumBin;
/// VI_VAR_STAT_RES_F2 = ( (m_stat[1].auraB.sline.sumL / m_stat[1].auraB.sline.cl) + (m_stat[1].auraB.sline.sumR / m_stat[1].auraB.sline.cr) ) / 2;
/// VI_VAR_STAT_RES_F3 = ( (m_stat[0].auraB.sline.sumL / m_stat[0].auraB.sline.cl) + (m_stat[0].auraB.sline.sumR / m_stat[0].auraB.sline.cr) ) / 2;
/// VI_VAR_STAT_RES_F4 =  max(m_stat[0]auraA.sline.maxL, m_stat[0]>auraA.sline.maxR);
/// VI_VAR_STAT_RES_F5 = m_statFFT.GetHfLf(VI_VAR_STAT_RES_F1);
/// if(disabled2X) VI_VAR_STAT_RES_F5X = m_statFFT.GetHfLf(VI_VAR_STAT_RES_F1X);
/// VI_VAR_STAT_RES_S1 = (m_stat[2].auraA.sline.sumL - m_stat[2].auraA.sline.sumR) / (m_stat[2].auraA.sline.cl + m_stat[2].auraA.sline.cr);
/// VI_VAR_STAT_RES_S2 = (m_stat[1].auraA.sline.sumL - m_stat[1].auraA.sline.sumR) / (m_stat[1].auraA.sline.cl + m_stat[1].auraA.sline.cr);
/// VI_VAR_STAT_RES_S2 = (m_stat[0].auraA.sline.sumL - m_stat[0].auraA.sline.sumR) / (m_stat[0].auraA.sline.cl + m_stat[0].auraA.sline.cr);
/// VI_VAR_STAT_RES_S4 = (m_stat[2].auraA.sline.cl - m_stat[2].auraA.sline.cr) / (m_stat[2].auraA.sline.cl + m_stat[2].auraA.sline.cr);
/// VI_VAR_STAT_RES_S5 = (m_stat[1].auraA.sline.cl - m_stat[1].auraA.sline.cr) / (m_stat[1].auraA.sline.cl + m_stat[1].auraA.sline.cr);
/// VI_VAR_STAT_RES_S6 = (m_stat[0].auraA.sline.cl - m_stat[0].auraA.sline.cr) / (m_stat[0].auraA.sline.cl + m_stat[0].auraA.sline.cr);
/// VI_VAR_STAT_RES_S7 = m_stat[0].auraA.sline.maxL - m_stat[0].auraA.sline.maxR;
/// VI_VAR_STAT_RES_P1 = m_stat[1].auraB.statCS;
/// VI_VAR_STAT_RES_P2 = m_stat[0].auraB.statCS;
/// VI_VAR_STAT_RES_P3 = m_stat[1].auraB.statSim;
/// VI_VAR_STAT_RES_P4 = m_stat[0].auraB.statSim;
/// hist = m_cfg.GetI1(VI_FILTER_HISTNW)? m_stat[0].auraA.statHistW :m_stat[0].auraA.statHist;
/// VI_VAR_STAT_RES_P8A = MakeCharming(hist.p, hist.s);
/// VI_VAR_STAT_RES_P9A = MakeEntropyH(hist.p,hist.s);
/// VI_VAR_STAT_RES_P10A = MakeEntropyD(hist.p,hist.s);
/// VI_VAR_STAT_RES_P11A = MakeEntropyX(hist.p,hist.s);
/// VI_VAR_STAT_RES_P12A = MakeEntropyS(hist.p,hist.s);
/// hist = m_cfg.GetI1(VI_FILTER_HISTNW)? m_stat[0].auraB.statHistW :m_stat[0].auraB.statHist;
/// VI_VAR_STAT_RES_P8F = MakeCharming(hist.p, hist.s);
/// VI_VAR_STAT_RES_P9F = MakeEntropyH(hist.p,hist.s);
/// VI_VAR_STAT_RES_P10F = MakeEntropyD(hist.p,hist.s);
/// VI_VAR_STAT_RES_P11F = MakeEntropyX(hist.p,hist.s);
/// VI_VAR_STAT_RES_P12F = MakeEntropyS(hist.p,hist.s);
/// VI_VAR_STAT_RES_P16 = MakeComN(hist.p, hist.s);
/// VI_VAR_STAT_RES_P17 = MakeComS(m_stat[0].auraA);
/// VI_VAR_STAT_RES_P18 = (VI_VAR_STAT_RES_P16 + VI_VAR_STAT_RES_P17) / 2;
/// </summary>
void CVIEngineBase::StatUpdate(void)
{
    SUMM_STAT *ps = 0;

    ps = &m_stat[0];
	m_cfg.PutF1(VI_VAR_STAT_INTEGR0A, ps->sumAin); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STAT_INTEGR0B, ps->sumBin); // Запись первого значения настроечного параметра по идентификатору ключа

    ps = &m_stat[1];
	m_cfg.PutF1(VI_VAR_STAT_INTEGR1A, ps->sumAin); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STAT_INTEGR1B, ps->sumBin); // Запись первого значения настроечного параметра по идентификатору ключа

    ps = &m_stat[2];
	m_cfg.PutF1(VI_VAR_STAT_INTEGR2A, ps->sumAin); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STAT_INTEGR2B, ps->sumBin); // Запись первого значения настроечного параметра по идентификатору ключа

    ps = &m_stat[0];

    BOOL disabled2X =m_cfg.GetI1(VI_FILTER_DISABLE_2X); // Чтение первого значения настроечного параметра по идентификатору ключа

    ///////////////////////////////
    // A1
    ///////////////////////////////
    m_cfg.PutF1( VI_VAR_STAT_RES_A1, ps->dsumAin );  // Запись первого значения настроечного параметра по идентификатору ключа
    if(disabled2X)
        m_cfg.PutF1( VI_VAR_STAT_RES_A1X, ps->dsumAin );  // Запись первого значения настроечного параметра по идентификатору ключа

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

        m_cfg.PutF1( VI_VAR_STAT_RES_A2,sum/2.0f ); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_A3, sum / 2.0f); // Запись первого значения настроечного параметра по идентификатору ключа
    }
    ///////////////////////////////

    ///////////////////////////////
    // A4
    ///////////////////////////////
	m_cfg.PutF1(VI_VAR_STAT_RES_A4, m_statAVG.Get(VI_VAR_STAT_RES_A1)); // Запись первого значения настроечного параметра по идентификатору ключа
    if(disabled2X)
		m_cfg.PutF1(VI_VAR_STAT_RES_A4X, m_statAVG.Get(VI_VAR_STAT_RES_A1X)); // Запись первого значения настроечного параметра по идентификатору ключа

    ///////////////////////////////
    // F1
    ///////////////////////////////
	m_cfg.PutF1(VI_VAR_STAT_RES_F1, ps->dsumBin); // Запись первого значения настроечного параметра по идентификатору ключа
    if(disabled2X)
    {
		m_cfg.PutF1(VI_VAR_STAT_RES_F1X, ps->dsumBin); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_F2, sum / 2.0f); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_F3, sum / 2.0f); // Запись первого значения настроечного параметра по идентификатору ключа
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
    if(!IsSkip()) // Признак того чтобы пропустить обработку кадра
    {
		m_cfg.PutF1(VI_VAR_STAT_RES_F5, m_statFFT.GetHfLf(VI_VAR_STAT_RES_F1)); // Запись первого значения настроечного параметра по идентификатору ключа

        if(disabled2X)
			m_cfg.PutF1(VI_VAR_STAT_RES_F5X, m_statFFT.GetHfLf(VI_VAR_STAT_RES_F1X)); // Запись первого значения настроечного параметра по идентификатору ключа
    } else
    {
		m_cfg.PutF1(VI_VAR_STAT_RES_F5, 0); // Запись первого значения настроечного параметра по идентификатору ключа

        if(disabled2X)
			m_cfg.PutF1(VI_VAR_STAT_RES_F5X, 0); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_S1, sum); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_S2, sum); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_S3, sum); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_S4, sum); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_S5, sum); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_S6, sum); // Запись первого значения настроечного параметра по идентификатору ключа
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
		m_cfg.PutF1(VI_VAR_STAT_RES_P1, ps->auraB.statCS); // Запись первого значения настроечного параметра по идентификатору ключа
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
		m_cfg.PutF1(VI_VAR_STAT_RES_P3, ps->auraB.statSim); // Запись первого значения настроечного параметра по идентификатору ключа
    }
    ///////////////////////////////

    ///////////////////////////////
    // P4
    ///////////////////////////////
    {
        ps = &m_stat[0];
		m_cfg.PutF1(VI_VAR_STAT_RES_P4, ps->auraB.statSim); // Запись первого значения настроечного параметра по идентификатору ключа
    }
    ///////////////////////////////


    ///////////////////////////////
    // P8A
    ///////////////////////////////
    {
        ps = &m_stat[0];
        mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
		m_cfg.PutF1(VI_VAR_STAT_RES_P8A, MakeCharming(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
    }
    ///////////////////////////////

    ///////////////////////////////
    // P8F
    ///////////////////////////////
    {
        ps = &m_stat[0];
        mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
		m_cfg.PutF1(VI_VAR_STAT_RES_P8F, MakeCharming(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
    }
    ///////////////////////////////


    if(! m_cfg.GetI1(VI_FILTER_DISABLE_ENTR) ) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
        ///////////////////////////////
        // P9A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P9A, MakeEntropyH(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P9F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P9F, MakeEntropyH(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P10A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P10A, MakeEntropyD(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P10F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P10F, MakeEntropyD(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P11A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P11A, MakeEntropyX(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P11F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P11F, MakeEntropyX(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P12A
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraA.statHistW : ps->auraA.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P12A, MakeEntropyS(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
        }
        ///////////////////////////////

        ///////////////////////////////
        // P12F
        ///////////////////////////////
        {
            ps = &m_stat[0];
            mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? ps->auraB.statHistW : ps->auraB.statHist;
			m_cfg.PutF1(VI_VAR_STAT_RES_P12F, MakeEntropyS(hist.p, hist.s)); // Запись первого значения настроечного параметра по идентификатору ключа
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

		m_cfg.PutF1(VI_VAR_STAT_RES_P16, CN); // Запись первого значения настроечного параметра по идентификатору ключа
		m_cfg.PutF1(VI_VAR_STAT_RES_P17, CS); // Запись первого значения настроечного параметра по идентификатору ключа
		m_cfg.PutF1(VI_VAR_STAT_RES_P18, (CN + CS)*0.5f); // Запись первого значения настроечного параметра по идентификатору ключа
    }
    ///////////////////////////////
}
/// <summary>
/// Метод для создания из "рабочей" нити Windows - то есть параллельного процесса для создания которого требуется только указатель на функцию которую надо выполнить и указатель на параметры
/// В качестве параметра передаётся указатель на инстанс данного класса.
/// Эта процедура рабочей нити выполняет только одно действие - вызывает метод AddImageThreadLocal8 инстанса данного класса.
/// </summary>
/// <param name="lpParameter">Указатель на параметры передаваемые при создании параллельной нити Windows, в данном случае - указатель на инстанс данного класса</param>
DWORD CVIEngineBase::AddImageThread8(LPVOID lpParameter)
{
    CVIEngineBase *pThis = (CVIEngineBase *)lpParameter;
    pThis->AddImageThreadLocal8();
    CloseHandle(pThis->m_hThreadAddImage8);
    pThis->m_hThreadAddImage8 = 0;
    return 0;
}

/// <summary>
/// Метод для создания из "рабочей" нити Windows - то есть параллельного процесса для создания которого требуется только указатель на функцию которую надо выполнить и указатель на параметры
/// В качестве параметра передаётся указатель на инстанс данного класса.
/// Эта процедура рабочей нити выполняет только одно действие - вызывает метод AddImageThreadLocal инстанса данного класса.
/// </summary>
/// <param name="lpParameter">Указатель на параметры передаваемые при создании параллельной нити Windows, в данном случае - указатель на инстанс данного класса</param>
DWORD CVIEngineBase::AddImageThread(LPVOID lpParameter)
{
    CVIEngineBase *pThis = (CVIEngineBase *)lpParameter;
    pThis->AddImageThreadLocal();
    CloseHandle(pThis->m_hThreadAddImage);
    pThis->m_hThreadAddImage = 0;
    return 0;
}

/// <summary>
/// Назначение процедуры - чтение кадров с требуемой частотой и запуск обработки, заданной VI_VAR_FPSMAXR.
/// Телодвижения в коде несовсем понятны, но в результате всё сводится к запуску MakeImage - процедуры обработки очередной порции кадров изображения.
/// Хотя по-ходу успевают к чему-то применить медианный фильтр - это фильтр подавления шумов на изображении.
/// Возвращает требуемую задержку в тиках ( 1000 тиков = 1 сек ) перед следующим запуском этой процедуры, рассчитанную на основе настроечного параметра VI_VAR_FPSMAXR, то есть чтобы обработка шла с заданным количеством кадров/сек.
/// Данный метод вызывается в цикле в методе AddImageThreadLocal, который вызывается в свою очередь из метода "рабочего" процесса Windows, запускаемого единственной параллельной нитью - то есть просто цикл в бэкграунде.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
int CVIEngineBase::AddImageThreadProc()
{
    CMTSingleLock lock(m_locks + LVI_SRC, false);

    DWORD tS,tE,tD; // Время начала работы данного процесса, время его завершения работы, продолжительность его работы
    std::list< SRC_IMG > cur;

    float fpsMax = m_cfg.GetF1(VI_VAR_FPSMAXR); // Максимальное количество обрабатываемых кадров/сек из настроек алгоритмов программы

	// Время начала работы данного процесса
	// GetTickCount function - Retrieves the number of milliseconds that have elapsed since the system was started, up to 49.7 days.
	tS = GetTickCount();

    if( m_cfg.GetI1(VI_FILTER_PAUSE) ) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
        return 10;
    }

	lock.Lock();  // Блокирование семафора LVI_SRC с ожиданием его освобождения другими процессами.

    BOOL bFpsDiv = (m_cfg.GetI1(VI_FILTER_FPSDIV)); // Чтение первого значения настроечного параметра по идентификатору ключа
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
        if(m_cfg.GetI1(VI_FILTER_FPS2IN)) // Чтение первого значения настроечного параметра по идентификатору ключа
        {
            m_divMaker.clear();
            div = m_cfg.GetI1(VI_FILTER_FPS2IN); // Чтение первого значения настроечного параметра по идентификатору ключа
        }
        else
            if(fpsMax > 0 && m_cfg.GetI1(VI_VAR_NFRAME_IN) > 2) // Чтение первого значения настроечного параметра по идентификатору ключа
            {
                float fpsIn = m_cfg.GetF1(VI_VAR_FPSIN); // Чтение первого значения настроечного параметра по идентификатору ключа
                div = round(fpsIn/fpsMax);
                if(div)
                    m_divMaker.push_back(div);
                div = Median(m_divMaker,9);
            }

            bool bSkip = false;

        if(m_cfg.GetI1(VI_VAR_NFRAME_IN) < 5) // Чтение первого значения настроечного параметра по идентификатору ключа
            bSkip = true;
        else
            if(div > 1)
            {
                int N = m_cfg.GetI1(VI_VAR_NFRAME_IN); // Чтение первого значения настроечного параметра по идентификатору ключа
                if(N%div != 0 || N < 5)
                    bSkip = true;
            }

            if(bSkip)
                return 1;
    }
    ///////////////////////////////////////////////////////

	m_cfg.PutI1(VI_VAR_FPS_BUFFER_SIZE, m_srcF.size()); // Запись первого значения настроечного параметра по идентификатору ключа
	lock.Unlock();  // Освобождение ранее заблокированного семафора LVI_SRC

    bool bMake = true;

    if(bMake && !m_bDone)
    {
        SRC_IMG& img = cur.front();
        MakeImage(img.i.begin(),img.ib.begin(),img.i.w,img.i.h); // Процедура обработки очередной порции кадров изображения.
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
/// Запуск метода AddImageThreadProc в цикле пока не скажут - Хватит! - while(!m_bDone && !m_bStop)
/// Если задан настроечный параметр VI_FILTER_FPSDIV, то пасле каждого запуска нить засыпает на указанное количество милисекунд но не более чем на 2 секунды.
/// Иначе вычисления приостанавливаются на 1/4 секунды.
/// Метод вызывается из "рабочей" нити Windows - то есть параллельного процесса для создания которого требуется только указатель на функцию которую надо выполнить
/// </summary>
/// <param name=""></param>
void CVIEngineBase::AddImageThreadLocal()
{
    while(!m_bDone && !m_bStop)
    {
        if(!m_cfg.GetI1(VI_FILTER_FPSDIV)) // Чтение первого значения настроечного параметра по идентификатору ключа
        {
            UINT dt = AddImageThreadProc();
            if(dt) Sleep(min(dt,2000));
        }
        else
            Sleep(250);

    }

    Sleep(250);

    CMTSingleLock lock(m_locks + LVI_SRC, false);
    lock.Lock();  // Блокирование семафора LVI_SRC с ожиданием его освобождения другими процессами.
    m_srcF.clear();
	lock.Unlock();  // Освобождение ранее заблокированного семафора LVI_SRC
}

/// <summary>
/// Обработка взведённых флагов сигналов в цикле пока не скажут - Хватит! - while(!m_bDone && !m_bStop)
/// То есть используется сигнально-событийная схема работы программы.
/// Для каждого сигнала предусмотрен свой обработчик.
/// По окончании обработки каждого сигнала взводится флаг m_evReady.
/// По проверяются и обрабатываются только флаги EVI_ADD8 и EVI_DONE.
/// В цикле производится ожидание до появление флага EVI_DONE.
/// Eсли не был поднят флаг EVI_ADD8, то вызывается метод AddImage8();
/// Всегда поднимается флаг готовности кадра  m_events[EVI_ADD8_READY].Set();
/// Метод вызывается из "рабочей" нити Windows - то есть параллельного процесса для создания которого требуется только указатель на функцию которую надо выполнить
/// </summary>
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
/// Останов вычислений.
/// Сохранение текущих значений настроечных параметров
/// Останов захвата изображения или звука
/// Останов дочерних потоков вычислений
/// </summary>
void CVIEngineBase::Stop(void)
{
    m_bStop = 1;
    m_cfg.RegSave(); // Сохранение текущих значений настроечных параметров

    m_audio.m_bDone = true; // Останов захвата изображения или звука

    Sleep(500);

    CloseThreads();

    while( m_audio.m_hThread )
        Sleep(5);

    #ifndef SEQ_DISABLE_FACE
    SAFE_DELETE(m_pFace);
    #endif
}


/// <summary>
/// Приостанов вычислений.
/// То есть в VI_FILTER_PAUSE записывается нужный признак.
/// Все процессы периодически считывают VI_FILTER_PAUSE и могут приостановить свои вычисления и расчёты или возобновить.
/// </summary>
/// <param name="bSet">Признак паузы или продолжения</param>
void CVIEngineBase::Pause(bool bSet)
{
	m_cfg.PutI1(VI_FILTER_PAUSE, bSet ? 1 : 0); // Запись первого значения настроечного параметра по идентификатору ключа
}


/// <summary>
/// Сброс, то есть фактически перезапуск работы приложения.
/// Действия:
/// Пауза;
/// Очиска результатов ClearStat();
/// Очиска элементов массива m_summ;
/// Обнуление буферов для хранения изображений;
/// Обнуление настроечных параметров из диапазона  [VI_STAT_START;VI_STAT_END];
/// Обнуление настроечных параметров из диапазона  [VI_STAT_EXT_START;VI_STAT_EXT_END];
/// Обнуление m_statFFT, m_statAVG, m_procF6;
/// Обнуление VI_VAR_NDROP;
/// Снятие с паузы;
/// Очистка параметра VI_VAR_RESET;
/// </summary>
/// <param name="bReset"></param>
void CVIEngineBase::Reset(bool bReset)
{
    if(!bReset && m_cfg.GetI1(VI_VAR_SIZE)) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
		m_cfg.PutI1(VI_VAR_RESET, 1); // Запись первого значения настроечного параметра по идентификатору ключа
        return;
    }

    Pause(true);

    ClearStat(); // Сброс, очистка и обнуление ранее вычисленных статистических значений.
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
		m_cfg.PutF1(k, 0); // Запись первого значения настроечного параметра по идентификатору ключа

    for( k = VI_STAT_EXT_START; k < VI_STAT_EXT_END; ++k )
		m_cfg.PutF1(k, 0); // Запись первого значения настроечного параметра по идентификатору ключа

    m_statFFT.Reset();
    m_statAVG.Reset();
    m_procF6.Reset();

	m_cfg.PutI1(VI_VAR_NDROP, 0); // Запись первого значения настроечного параметра по идентификатору ключа

    Pause(false);

	m_cfg.PutI1(VI_VAR_RESET, 0); // Запись первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Реализация метода не содержит программного кода.
/// </summary>
/// <param name="src"></param>
/// <param name="sw"></param>
/// <param name="sh"></param>
void CVIEngineBase::MakeStatFS2(float* src, int sw, int sh)
{
}

/// <summary>
/// Сохранение текущих значений FPS - аттрибуты типа CStatFPS - в настроечные параметры.
///		VI_VAR_FPSIN;
///		VI_VAR_FPSOUTF;
///		VI_VAR_FPSOUTR;
///		VI_VAR_FPSDROPF;
///		VI_VAR_FPSDROPR;
/// </summary>
void CVIEngineBase::FlushFPS(void)
{
    float fpsIn = m_fpsIn.Get();
    float fpsOutF = m_fpsOutF.Get();
    float fpsOutR = m_fpsOutR.Get();
	m_cfg.PutF1(VI_VAR_FPSIN, fpsIn); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_FPSOUTF, min(fpsIn, fpsOutF)); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_FPSOUTR, min(fpsIn, fpsOutR)); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_FPSDROPF, m_fpsDropF.Get()); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_FPSDROPR, m_fpsDropR.Get()); // Запись первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Начальная инициализация инстанса класса
/// Изменение размеров массивов m_stat, m_summ, m_aura6A, m_aura6B до нового размера == 3. 
/// Присвоение m_summ[i].id =  { VI_VAR_N0, VI_VAR_N1, VI_VAR_N2 }[i] 
/// </summary>
void CVIEngineBase::Start(void)
{
    m_cfg.Init();
    #ifndef SEQ_DISABLE_LD
    m_statLDF.Init();
    #endif

    int id[3] = { VI_VAR_N0, VI_VAR_N1, VI_VAR_N2 };
    SetSummCount(3,id); // Изменение размеров массивов m_stat, m_summ, m_aura6A, m_aura6B до нового размера. Присвоение m_summ[i].id = pN[i] 
}

/// <summary>
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
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

    n0c = m_cfg.GetI1(VI_VAR_N0); // Чтение первого значения настроечного параметра по идентификатору ключа
    n1c = m_cfg.GetI1(VI_VAR_N1); // Чтение первого значения настроечного параметра по идентификатору ключа
    n2c = m_cfg.GetI1(VI_VAR_N2); // Чтение первого значения настроечного параметра по идентификатору ключа

    n0r = m_cfg.GetI1(VI_VAR_N0_RQST); // Чтение первого значения настроечного параметра по идентификатору ключа
    n1r = m_cfg.GetI1(VI_VAR_N1_RQST); // Чтение первого значения настроечного параметра по идентификатору ключа
    n2r = m_cfg.GetI1(VI_VAR_N2_RQST); // Чтение первого значения настроечного параметра по идентификатору ключа

    if( n0c != n0r ) bNew = true;
    if( n1c != n1r ) bNew = true;
    if( n2c != n2r ) bNew = true;


    if( !bNew )
        return false;

    Reset(true);

	m_cfg.PutI1(VI_VAR_N0, n0r); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutI1(VI_VAR_N1, n1r); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutI1(VI_VAR_N2, n2r); // Запись первого значения настроечного параметра по идентификатору ключа

    int mCnt = max( n0r, max(n1r,n2r) );
    if(mCnt < 2)
        mCnt = 2;
    SetSize(w,h,mCnt);

    return true;
}


/// <summary>
/// Вывод результата на дисплей.
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
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

    MakeFaceDraw(); // Вывод картинки на экран через API в классе CVIEngineFace.

    ++m_resultVer;
}

/// <summary>
/// Получение таблицы гистограммы яркостий пикселей по вычисленному изображению (вариант N).
/// Так же проверяется что *pFPS не пустое, в противном случае заполняется значением из нестроечного параметра VI_VAR_FPSOUTR.
/// В предоставленном коде вызовов метода нет.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
/// <param name="pHist256">
/// Указатель на гистограмму монохромного изображения, то есть на таблицу количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до 255
/// </param>
/// <param name="pFPS">Указатель на проверяемое-возвращаемое значение частоты кадров/сек</param>
int CVIEngineBase::GetStatHistN(int res, int* pHist256, float *pFPS)
{
    int nSum = res2n(res); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res.

    if(m_statRelease.empty())
        return false;

    AURA_STAT & stat = IsModeA(res) ? m_statRelease[nSum].auraA : m_statRelease[nSum].auraB;
    mmx_array<int>& hist = m_cfg.GetI1(VI_FILTER_HISTNW)? stat.statHistW : stat.statHist;

    if(hist.empty())
        return 0;
    if(pHist256)
        hist.exportto(pHist256);
    if(pFPS) *pFPS = m_cfg.GetF1( VI_VAR_FPSOUTR ); // Чтение первого значения настроечного параметра по идентификатору ключа
    return m_cfg.GetI1(VI_VAR_NFRAME); // Чтение первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Получение таблицы гистограммы яркостий пикселей по вычисленному изображению (вариант C).
/// Так же проверяется что *pFPS не пустое, в противном случае заполняется значением из нестроечного параметра VI_VAR_FPSOUTR.
/// В предоставленном коде вызовов метода нет.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
/// <param name="pHist256">
/// Указатель на гистограмму монохромного изображения, то есть на таблицу количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до 255
/// </param>
/// <param name="pFPS">Указатель на проверяемое-возвращаемое значение частоты кадров/сек</param>
int CVIEngineBase::GetStatHistC(int res, int* pHist256, float *pFPS)
{
    int nSum = res2n(res); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res.

    if(m_statRelease.empty())
        return false;
    AURA_STAT & stat = IsModeA(res) ? m_statRelease[nSum].auraA : m_statRelease[nSum].auraB;
    mmx_array<int>& hist = stat.statHistA;
    if(hist.empty())
        return 0;
    if(pHist256)
        hist.exportto(pHist256);
    if(pFPS) *pFPS = m_cfg.GetF1( VI_VAR_FPSOUTR ); // Чтение первого значения настроечного параметра по идентификатору ключа
    return m_cfg.GetI1(VI_VAR_NFRAME); // Чтение первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Получение таблицы гистограммы яркостий пикселей по вычисленному изображению (вариант F).
/// Так же проверяется что *pFPS не пустое, в противном случае заполняется значением из нестроечного параметра VI_VAR_FPSOUTR.
/// В предоставленном коде вызовов метода нет.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
/// <param name="pHist256">
/// Указатель на гистограмму монохромного изображения, то есть на таблицу количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до 255
/// </param>
/// <param name="pFPS">Указатель на проверяемое-возвращаемое значение частоты кадров/сек</param>
int CVIEngineBase::GetStatHistF(int res, int* pHist256, float *pFPS)
{
    int nSum = res2n(res); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res.
    if(m_statRelease.empty())
        return false;
    AURA_STAT & stat = IsModeA(res) ? m_statRelease[nSum].auraA : m_statRelease[nSum].auraB;
    mmx_array<int>& hist = stat.statHistC;
    if(hist.empty())
        return 0;
    if(pHist256)
        hist.exportto(pHist256);
    if(pFPS) *pFPS = m_cfg.GetF1( VI_VAR_FPSOUTR ); // Чтение первого значения настроечного параметра по идентификатору ключа
    return m_cfg.GetI1(VI_VAR_NFRAME); // Чтение первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Получение таблицы гистограммы яркостий пикселей по вычисленному изображению (вариант FT).
/// В предоставленном коде вызовов метода нет.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
/// <param name="pHist256">
/// Указатель на гистограмму монохромного изображения, то есть на таблицу количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до 255
/// </param>
/// <param name="pDT"></param>
int CVIEngineBase::GetStatHistFT(int res, int* pHist256, float *pDT)
{
    if(pDT) *pDT = m_cfg.GetF1( 1.0f/VI_VAR_FPSOUTF ); // Чтение первого значения настроечного параметра по идентификатору ключа
    if(res == VI_VAR_HIST_F6)
        return m_procF6.GetHist(pHist256,pDT,m_procF6.m_bufRes);
    if(res == VI_VAR_HIST_F8)
        return m_procF6.GetHist(pHist256,pDT,m_procF6.m_bufSrc);
    return 0;
}

/// <summary>
/// Вызов одноимённого метода с параметрами для режима B и для режима A.
///		MakeAnger(true);
///		MakeAnger(false);
/// </summary>
void CVIEngineBase::MakeAnger(void)
{
    MakeAnger(true);
    MakeAnger(false);
}

/// <summary>
/// </summary>
/// <param name="bModeB">Признак формирования результата для режима B</param>
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
    if(!IsSkip()) // Признак того чтобы пропустить обработку кадра
        anger = (Fm + 4.0f*S)/512.0f;

    if(bModeB)
		m_cfg.PutF1(VI_VAR_STAT_RES_P7F, anger); // Запись первого значения настроечного параметра по идентификатору ключа
    else
		m_cfg.PutF1(VI_VAR_STAT_RES_P7A, anger); // Запись первого значения настроечного параметра по идентификатору ключа
}

/// <summary>
/// Вызов одноимённого метода с параметрами для режима B и для режима A.
///		MakeStress(true);
///		MakeStress(false);
/// </summary>
void CVIEngineBase::MakeStress(void)
{
    MakeStress(true);
    MakeStress(false);
}

/// <summary>
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
/// <param name="bModeB">Признак формирования результата для режима B</param>
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
		m_cfg.PutF1(VI_VAR_STAT_RES_P6F, stress); // Запись первого значения настроечного параметра по идентификатору ключа
    else
		m_cfg.PutF1(VI_VAR_STAT_RES_P6A, stress); // Запись первого значения настроечного параметра по идентификатору ключа
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
    double t = m_timer.Get();  // Нормализованное в секунды значение часов
    double n = m_cfg.GetF1(VI_VAR_STAT_CFG_SIN); // Чтение первого значения настроечного параметра по идентификатору ключа
    double v = sin( t*2*M_PI*n );
	m_cfg.PutF1(VI_VAR_STAT_RES_SIN, (float)v); // Запись первого значения настроечного параметра по идентификатору ключа
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
/// <param name="res">Набор двоичных флагов</param>
void CVIEngineBase::tmp_aura_draw(int res)
{
    int n = res2n(res); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res.
    bool bModeA = IsModeA(res);  // Вычисление признака работы в режиме A согласно списку допустимых значений переменной
    CVIEngineAura6 &aura6 = bModeA ? m_aura6A[n] : m_aura6B[n];

    int w,h;
    void *pi;
    if(! GetResultPtr(res,&pi,&w,&h) )
        return;

    aura6.MakeExportSum((DWORD*)pi,(COLORREF*)m_palI,w,h,0,0);
}

/// <summary>
/// Проверка что настроечный параметр с указанным id установлен, и, в противном случае, выполнение операции Reset.
/// </summary>
/// <param name="id">Идентификатор настроечного параметра</param>
void CVIEngineBase::OnNewVarDidable(int id)
{
    int v = m_cfg.GetI1(id); // Чтение первого значения настроечного параметра по идентификатору ключа
    if(!v)
        return;

    Reset();
}

/// <summary>
/// Чтение настройки для процедуры сенсора определения факта движения на основе ранее расчитанных статистических данных
/// Если bSet == false и VI_FILTER_DISABLE_VI1 ненеулевой, то VI_FILTER_MOTION_SET сбрасывается и возвращается 0
/// Иначе если VI_FILTER_MOTION не указан, то VI_FILTER_MOTION_SET сбрасывается и возвращается 0
/// Иначе производится ряд расчётов по настроечным параметрам, результат записывается в VI_FILTER_MOTION_SET и возвращается вычисленная величина
/// </summary>
/// <param name="bSet">Признак необходимости пересчёта значения настроечного параметра</param>
int CVIEngineBase::IsMotion(bool bSet)
{
    if(!bSet)
    {
        if(m_cfg.GetI1(VI_FILTER_DISABLE_VI1)) // Чтение первого значения настроечного параметра по идентификатору ключа
			m_cfg.PutI1(VI_FILTER_MOTION_SET, 0); // Запись первого значения настроечного параметра по идентификатору ключа
        return m_cfg.GetI1(VI_FILTER_MOTION_SET); // Чтение первого значения настроечного параметра по идентификатору ключа
    }

    if(!m_cfg.GetI1(VI_FILTER_MOTION)) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
		m_cfg.PutI1(VI_FILTER_MOTION_SET, 0); // Запись первого значения настроечного параметра по идентификатору ключа
        return 0;
    }

    float lev = m_cfg.GetF1(VI_FILTER_MOTION_LEVEL); // Чтение первого значения настроечного параметра по идентификатору ключа
    float v = m_cfg.GetI1(VI_FILTER_MOTION_10X) ? m_cfg.GetF1(VI_VAR_STAT_INTEGR1A):m_cfg.GetF1(VI_VAR_STAT_INTEGR0A);
    if(v <= lev)
    {
        int set = m_cfg.GetI1(VI_FILTER_MOTION_SET); // Чтение первого значения настроечного параметра по идентификатору ключа
        int n = (set > 0) ? set+1 : 1;
		m_cfg.PutI1(VI_FILTER_MOTION_SET, n); // Запись первого значения настроечного параметра по идентификатору ключа
        return n;
    } else
    {
        float lev2 = m_cfg.GetF1(VI_FILTER_MOTION_LEVEL2); // Чтение первого значения настроечного параметра по идентификатору ключа
        float fi10 = m_cfg.GetF1(VI_VAR_STAT_INTEGR1A); // Чтение первого значения настроечного параметра по идентификатору ключа
        float fiN = m_cfg.GetF1(VI_VAR_STAT_INTEGR0A); // Чтение первого значения настроечного параметра по идентификатору ключа

        if( fi10 > lev2 || (fi10 > lev2/2.0f && fi10 > fiN * 5.0f) )
        {
            int set = m_cfg.GetI1(VI_FILTER_MOTION_SET); // Чтение первого значения настроечного параметра по идентификатору ключа
            int n = (set < 0) ? set-1 : -1;
			m_cfg.PutI1(VI_FILTER_MOTION_SET, n); // Запись первого значения настроечного параметра по идентификатору ключа
            return n;
        }
    }

	m_cfg.PutI1(VI_FILTER_MOTION_SET, 0); // Запись первого значения настроечного параметра по идентификатору ключа
    return 0;
}

/// <summary>
/// Вызов метода Reset() если выполняется условие от процедуры сенсора движения.
/// Получение текущего параметра для процедуры сенсора движения noise = IsMotion(true);
/// Проверка необходимости вызова процедуры Reset - т.е. проверка условия noise == 3 || noise == -2 || m_statRelease[1].cntAin > 0.7f;
/// Вызов  Reset() если условие выполняется
/// </summary>
void CVIEngineBase::MakeMotion(void)
{
    int noise = IsMotion(true); /// Получение текущего параметра для процедуры сенсора движения
    bool bReset = false;
    if(m_cfg.GetI1(VI_FILTER_MOTION_AUTO_RESET)) // Чтение первого значения настроечного параметра по идентификатору ключа
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
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
float CVIEngineBase::MakeStateMacro(void)
{
    #ifndef SEQ_DISABLE_MACRO_MODE_PLUS
    #ifndef SEQ_DISABLE_FACE
    if( m_cfg.GetI1(VI_MACRO_FACE) ) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
        if( m_cfg.GetI1(VI_FACE_ENABLE)&& m_pFace) // Чтение первого значения настроечного параметра по идентификатору ключа
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
    lL = m_cfg.GetI1(VI_MACRO_LEVEL_L); // Чтение первого значения настроечного параметра по идентификатору ключа
    lS = m_cfg.GetF1(VI_MACRO_LEVEL_S)/100.0; // Чтение первого значения настроечного параметра по идентификатору ключа

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

	m_cfg.PutF1(VI_VAR_STAT_RES_P13, (float)fF); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STAT_RES_P14, (float)fNr); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STAT_RES_P15, (float)fNs); // Запись первого значения настроечного параметра по идентификатору ключа

    if(N < Nn || !N || lL >= 255) // малая площадь
        return 0;

    return (float)( (fF + fNr) / 2 );
}

/// <summary>
/// Выбор коэффициента либо из настроек, либо рассчитать на основе вычисленных статистик, вызовом одноимённой функции с параметрами, сохранение параметров.
/// if(state >= lev)
///	{
///		m_cfg.PutI1(VI_VAR_STATE_FLAG_A, 1);
///		m_cfg.PutI1(VI_VAR_STATE_FLAG_P, 1);
///	}
///	 else
///		 m_cfg.PutI1(VI_VAR_STATE_FLAG_A, 0);
/// </summary>
float CVIEngineBase::MakeState(void)
{
    float state0 = 0;

    CMTSingleLock *pFaceLock = 0;

    if(m_cfg.GetI1(VI_MACRO_ENABLE)) // Чтение первого значения настроечного параметра по идентификатору ключа
    {
        state0 =  MakeStateMacro();
    }
    else
    {

        float Ag = 0;
        float St = 0;
        float Tn = m_cfg.GetF1(VI_VAR_STAT_RES_F5X); // Чтение первого значения настроечного параметра по идентификатору ключа

        #ifndef SEQ_LITE
        Ag = m_cfg.GetF1(VI_VAR_STAT_RES_P7); // Чтение первого значения настроечного параметра по идентификатору ключа
        St = m_cfg.GetF1(VI_VAR_STAT_RES_P6); // Чтение первого значения настроечного параметра по идентификатору ключа
        #else

        Ag = m_cfg.GetF1(VI_VAR_STAT_RES_P7A); // Чтение первого значения настроечного параметра по идентификатору ключа
        St = m_cfg.GetF1(VI_VAR_STAT_RES_P6A); // Чтение первого значения настроечного параметра по идентификатору ключа
        #endif
        state0 = MakeState(Ag,St,Tn); // Выбор коэффициента либо среднее параметров, либо 0.8, либо 1.0
        MakeStateMacro();
    }

    if(state0 < 0)
        state0 = 0;

	m_cfg.PutF1(VI_VAR_STATE_VAR_SRC, state0); // Запись первого значения настроечного параметра по идентификатору ключа

    float state = m_statAVG.Get(VI_VAR_STATE_VAR_SRC);
    if(state < 0)
        state = 0;

    float lev = m_cfg.GetF1(VI_VAR_STATE_CRITICAL); // Чтение первого значения настроечного параметра по идентификатору ключа

	m_cfg.PutF1(VI_VAR_STATE_CRITICAL, lev); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STATE_VAR, state); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutF1(VI_VAR_STAT_RES_P19, state); // Запись первого значения настроечного параметра по идентификатору ключа

    if(state >= lev)
    {
		m_cfg.PutI1(VI_VAR_STATE_FLAG_A, 1); // Запись первого значения настроечного параметра по идентификатору ключа
		m_cfg.PutI1(VI_VAR_STATE_FLAG_P, 1); // Запись первого значения настроечного параметра по идентификатору ключа
    } else
		m_cfg.PutI1(VI_VAR_STATE_FLAG_A, 0); // Запись первого значения настроечного параметра по идентификатору ключа

    return state;
}

/// <summary>
/// Выбор коэффициента либо среднее параметров, либо 0.8, либо 1.0
/// Код. Просто код. Лучше чем этот код не объяснишь.
/// const float dAg = 0.75f;
/// const float dSt = 0.80f;
/// const float dTn = 0.60f;
/// int ret = 0;
/// if (Ag > dAg) ret |= 1;
/// if (St > dSt) ret |= 2;
/// if (Tn > dTn) ret |= 4;
/// switch (ret)
/// {
/// case 0:
/// 	return (Ag + St + Tn) / 3.0f;
/// case 1:
/// case 2:
/// case 4:
/// 	return 0.8f;
/// case 3:
/// case 5:
/// case 6:
/// case 7:
/// 	return 1.00f;
/// default:
/// 	break;
/// }
/// return 0;
/// </summary>
/// <param name="Ag"></param>
/// <param name="St"></param>
/// <param name="Tn"></param>
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
/// Формула по гистограмме
/// Формула:
/// sV = СУММА pHist256(i), 
/// sVX = СУММА i * pHist256(i), 
/// M = sVX / sV;
/// d(i) = i*fps/len-M;
/// sDV = d(i)*d(i)*pHist256(i);
/// D = sDV / sV;
/// S = sqrt(D);
/// return (M-S)/fps;
/// Используется только в CVIEngineBase::StatUpdate(void)
/// </summary>
/// <param name="pHist256">
/// Гистограмма монохромного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до len-1
/// </param>
/// <param name="len">
/// Размер массива.
/// Фактически является верхней границей яркости исходного монохромного изображения+1
/// </param>
float CVIEngineBase::MakeCharming(int* pHist256, int len)
{
    int i;
    double flen = (double)len;
    double M,S,D,CH;

    double fps = m_cfg.GetF1(VI_VAR_FPSOUTR); // Чтение первого значения настроечного параметра по идентификатору ключа

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
/// Вычисление значения энтропии по гистограмме сигнала (Вариант Н - заимствование идеи из статистического критерия наиболее мощный)
/// Термин энтропия означает численное обозначение некоторой неопределённости и используется в различных областях науки для анализа данных
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// СУММА pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Формула:
/// n = СУММА pHist256(i), 
/// sum = СУММА ( pHist256(i) / n ) * log10( n * d / pHist256(i) ), 
/// return sum;
/// где при подсчёте сумм берутся все i кроме pHist256(i) == 0 или i == 0
/// </summary>
/// <param name="pHist256">
/// Гистограмма монохромного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до len-1
/// </param>
/// <param name="len">
/// Размер массива.
/// Фактически является верхней границей яркости исходного монохромного изображения+1
/// </param>
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
/// Вычисление значения энтропии по гистограмме сигнала (Вариант D - заимствование идеи из ... видимо совсем собственное творчество)
/// Термин энтропия означает численное обозначение некоторой неопределённости и используется в различных областях науки для анализа данных
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// СУММА pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Формула:
/// d = 1.0/len,
/// n = СУММА pHist256(i), 
/// sm = СУММА pHist256(i) * log10( pHist256(i) ), 
/// Delta = (d*n/2)*pow(10,-1.0*sm/n),
/// return Delta;
/// где при подсчёте сумм берутся все i кроме pHist256(i) == 0 или i == 0
/// </summary>
/// <param name="pHist256">
/// Гистограмма монохромного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до len-1
/// </param>
/// <param name="len">
/// Размер массива.
/// Фактически является верхней границей яркости исходного монохромного изображения+1
/// </param>
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
/// Вычисление значения энтропии по гистограмме сигнала (Вариант X - заимствование идеи из статистического критерия X^2)
/// Термин энтропия означает численное обозначение некоторой неопределённости и используется в различных областях науки для анализа данных
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// СУММА pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Затем по этой таблице был вычислено среднее значение M яркости ненулевых элементов (= математическое ожидание без учёта нулевых значений)
/// Для каждого элементв вычисленно отклонение от линейно-равномерного распределения, то есть
/// Если flen - максимальное значение яркости, то для каждого ненулевого значения i вычисляются коэффициенты dd(i) = (i/flen - M)^2
/// И вычисляется значение return ( СУММА dd(i)*pHist256(i) / СУММА pHist256(i) ) / sqrt( СУММА dd(i)*dd(i)*pHist256(i) / СУММА pHist256(i) )
/// </summary>
/// <param name="pHist256">
/// Гистограмма монохромного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
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

        sV  += v; // Накопление СУММА pHist256(i) = количество пикселей в исходном изображении
        sVX += v*X; // Накопление СУММА i*pHist256(i)/flen - сумма яркостей всех пикселей, если яркость записывать значениями от 0.0 до 1.0
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
        sDV += dd*v; // Накопление СУММА dd(i)*pHist256(i)
		sDV2 += dd*dd*v; // Накопление СУММА dd(i)*dd(i)*pHist256(i)
    }

    D = sDV/sV;
    D2 = sDV2/sV;

    double E = D/sqrt(D2); // return СУММА dd(i)*pHist256(i) / sqrt( СУММА dd(i)*dd(i)*pHist256(i))

    return (float)E;
}

/// <summary>
/// Вычисление значения энтропии по гистограмме сигнала (Вариант S - заимствование идеи у инженера-телеграфиста Шенона, работавшего в начале прошлого века в компании Bell)
/// Термин энтропия означает численное обозначение некоторой неопределённости и используется в различных областях науки для анализа данных
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// СУММА pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Сперва в таблице pHist256 находим элемент i с наиболее часто вcтречающейся яркостью 
/// и для него вычисляется Pm - количество таких пикселей и Xm = m/len = то есть значение яркости выраженное от 0.0 до 1.0
/// (максимальнвя яркость на данном монохромном изображении равна 1.0)
/// И вычисляется значение return log( СУММА abs( (i*fps/len - Xm) * ( pHist256(i) - Pm ) ) / len )
/// где  fps - количество кадров/сек из настроечных параметров алгоритмов (параметр VI_VAR_FPSOUTR)
/// В данной реализации элементы i, для которых pHist256(i) == 0 или i == 0, не участвуют в расчётах суммы
/// </summary>
/// <param name="pHist256">
/// Гистограмма монохромного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
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
    double fps = m_cfg.GetF1(VI_VAR_FPSOUTR); // Чтение первого значения настроечного параметра по идентификатору ключа

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
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="file">Имя файла</param>
bool CVIEngineBase::SrcMaskLoad(LPCWSTR file)
{
    CImage img;
    if(img.Load(file) != S_OK)
        return false;
    int w = img.GetWidth();
    int h = img.GetHeight();

	CMTSingleLock   xlock(m_locks + LVI_SRC8, true);  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.
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
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="file">Имя файла</param>
bool CVIEngineBase::SrcMaskSave(LPCWSTR file)
{
	CMTSingleLock   xlock(m_locks + LVI_SRC8, true);  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.
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
/// Очистка двумерного массива m_imgSrcMask
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
bool CVIEngineBase::SrcMaskReset()
{
	CMTSingleLock   xlock(m_locks + LVI_SRC8, true);  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.
    m_imgSrcMask.clear();
    return true;
}

/// <summary>
/// Присвоение нулевого значения элементу двумерного массива m_imgSrcMask по указанным координатам.
/// Если массив m_imgSrcMask пустой то он создаётся с размерами взятыми у m_imgSrc8 и заполняется байтами 0xFF.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
/// <param name="x">x координата элемента</param>
/// <param name="y">y координата элемента</param>
bool CVIEngineBase::SrcMaskErase(int x, int y)
{
	CMTSingleLock   xlock(m_locks + LVI_SRC8, true);  // Блокирование семафора LVI_SRC8 с ожиданием его освобождения другими процессами.

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
/// Вычисление значения статистики по гистограмме сигнала.
/// Гистограмма - это вторичная спецификация исходных данный, являющаяся просто подсчётом сколько раз встретился в исходных данных каждый образец.
/// В данном случае исходными данными являются значения яркостей пискелей монохромного изображения, выраженых числами от 0 до 255
/// Для каждого значения i было подсчитано сколько раз оно встретилось и было занесено в таблицу pHist256 по соответствующему адресу i
/// СУММА pHist256(i) = количество пикселей исходного изображения = ширина*высота
/// Формула:
/// nPI = (0.5/M_PI)*(0.5/M_PI);
/// M = ( СУММА i * pHist256(i) ) / ( СУММА pHist256(i) );
/// dx(i) =  pHist256(i) - M;
/// t(i) =  i - M;
/// z(i) =  nPI*exp(-0.5*t(i)*t(i));
/// SumZ = СУММА nPI*exp(-0.5*t(i)*t(i));
/// S = sqrt( ( СУММА dx(i) * dx(i) * pHist256(i) ) / ( СУММА pHist256(i) ) );
/// K = ( СУММА z(i) ) / ( СУММА pHist256(i) );
/// return max( 0.0, min( sqrt( СУММА (pHist256[i] * K - z(i))^2 ) / sqrt( СУММА z(i)^2 ) , 1.0 ) );
/// </summary>
/// <param name="pHist256">
/// Гистограмма монохромного изображения, то есть таблица количества пискелей монохромного изображения имеющих заданную яркость.
/// При этом полагается что монохромное изображение было предствлено в виде массива целых чисел в диапазоне от 0 до len-1
/// </param>
/// <param name="len">
/// Размер массива.
/// Фактически является верхней границей яркости исходного монохромного изображения+1
/// </param>
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
/// Вычисление средней величины по значениям ранее рассчитанных строк в структуре AURA_STAT.
/// Теоретический смысл формулы непонятен.
/// nl = aura.line.size();
/// sW = СУММА abs(aura.line[y].cwl-aura.line[y].cwr)/max(aura.line[y].cwl,aura.line[y].cwr);
/// sC = СУММА abs(aura.line[y].aColorL - aura.line[y].aColorR)/255.0f;
/// return 1.0f - (sC+sW)/(2*nl);
/// </summary>
/// <param name="aura">Ссылка на инстанс структуры AURA_STAT</param>
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
/// Заполнение массивов pCWL и pCWR ранее вычисленными значениями статистики ауры (длинами линий);
/// Заполнение массивов pCR и pCL ранее вычисленными значениями цветами ауры;
/// Данные структуры AURA_STAT берутся AURA_STAT &aura = bB ? m_statRelease[nProc].auraB : m_statRelease[nProc].auraA;
/// Количество обрабатываемых элементов int cnt = aura.line.size();
/// </summary>
/// <param name="pCWL">Указатель на массив длин линий для левой части</param>
/// <param name="pCWR">Указатель на массив длин линий для правой части</param>
/// <param name="pCR">Указатель на массив цветов для левой части</param>
/// <param name="pCL">Указатель на массив цветов для правой части</param>
/// <param name="nProc">Индекс элемента в массиве m_statRelease</param>
/// <param name="bB">Признак вывода результата режима B</param>
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
/// Проверка возможности построения гистограммы указаного типа.
/// Всегда возвращает true.
/// В известном коде программы не используется.
/// </summary>
/// <param name="id">Идентификатор типа гистограммы</param>
bool CVIEngineBase::CanMakeHist(int id)
{
    return true;
}

/// <summary>
/// Вывод картинки на экран через API в классе CVIEngineFace.
/// Берётся одна из картинок (какая есть в наличии)
///		m_resultPtr[VI_RESULT_SRC_0];
///		m_resultPtr[VI_RESULT_SRC_A];
///		m_resultPtr[VI_RESULT_SRC_B];
/// Размер исходного изображения берётся из настроечного параметра VI_VAR_SIZE.
/// </summary>
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
        m_pFace->MakeDraw(imgRes,w,h); // Вывод картинки на экран.

    imgRes = (RGBQUAD*)(m_resultPtr[VI_RESULT_SRC_A]);
    if(imgRes)
        m_pFace->MakeDraw(imgRes,w,h); // Вывод картинки на экран.

    imgRes = (RGBQUAD*)(m_resultPtr[VI_RESULT_SRC_B]);
    if(imgRes)
        m_pFace->MakeDraw(imgRes,w,h); // Вывод картинки на экран.
    #endif
}

/// <summary>
/// Подготовки различных внутренних структур к готовности получать данные от устройства захвата звука или изображения.
/// По сути является вызовом одноимённого метода класса CVIEngineAudio2 для аттрибута m_audio + обнуление некоторых настроечных параметров.
/// Поддерживает блокирование ресурсов для возможности использования функции из нескольких параллельных процессов.
/// </summary>
void CVIEngineBase::NewSource()
{
	m_audio.NewSource(); // Подготовки различных внутренних структур к готовности получать данные от устройства захвата звука или изображения.

	CMTSingleLock   lock(m_locks + LVI_ALL, true);  // Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.
    Reset(true);

	m_cfg.PutI1(VI_VAR_NFRAME, 0); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutI1(VI_VAR_NFRAME_IN, 0); // Запись первого значения настроечного параметра по идентификатору ключа
	m_cfg.PutI1(VI_VAR_NFRAME_OUTF, 0); // Запись первого значения настроечного параметра по идентификатору ключа

    m_tVideoDT = 0;
	m_cfg.PutI1(VI_VAR_NDROP, 0); // Запись первого значения настроечного параметра по идентификатору ключа

}
