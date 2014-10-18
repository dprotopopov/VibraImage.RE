#pragma once

#include "VIEngineEvent.h"
#include "PointSSE.h"
#include "viEngine_types.h"
#include "VIEngineAura6.h"

class CVIEngineBase; // Класс алгоритмов обработки изображений программы
class CVIEngineConfig; // Класс настроечных параметров программы

/// <summary>
/// Класс для реализации расчётов в многопоточной среде.
/// Является классом для управления одним потоком расчётов.
/// Ускорение расчётов достигается за счёт создания нескольких инстансов данного класса (потоков вычисления), одновременно производящмх общёт изображения.
/// Разделение заданий происходит с помощью разделения между потоками диапазонов строк обрабатываемого изображения.
/// Диапазон обрабатываемых строк инстансом данного класа задаётся членами класса m_yS,m_yE.
/// Аналогичное разделение заданий между процессами реализованно в проекте https://github.com/dprotopopov/MedianFilter.
/// Сужествуют и другие способы разделения заданий - но от перемены слагаемых сумма не меняется.
/// </summary>
class CVIEngineThread
{
public:
    typedef struct tagSUMM_PTR
    {
        int n, nLast;

        SUMM_STAT * pStat;

        float *pSummA, *pSummB;
        float *pCSummA,*pCSummB;
        float *pDeltaA;
        float *pCDeltaA;

        float *pRetA, *pRetB;
        float *pCRetA,*pCRetB;

        short *pRetIA,*pRetIB;
        short *pCRetIA,*pCRetIB;


		float   divA;
		float   divB;
    } SUMM_PTR;

	/// <summary>
	/// Структура для хранения статистики по вычисленному фрагменту дельта-изображения для одного процесса
	/// </summary>
	typedef struct tagTMP_STAT
    {
        mmx_array2<float>   sumN8A;
        mmx_array2<float>   sumN8B;
        mmx_array<float>    sumN8XA;
        mmx_array<float>    sumN8XB;
        mmx_array<float>    sumN8YA;
        mmx_array<float>    sumN8YB;

		float               dsumA; // Cумма значений яркости всех пикселей в дельта-изображении после применения фильтров подавления шумов.
		float               dsumB; // Количество пикселей с ненулевыми значениями яркости в дельта-изображении после применения фильтров подавления шумов.
        float               sumAi,sumBi;

        float               cntAi,cntBi;

    } TMP_STAT;


    typedef struct tagTMP_AURA_STAT
    {
        mmx_array<int>  statHist/*(256)*/;  // гистограмма цветов
        mmx_array<int>  statHistA/*(256)*/; // гистограмма цветов по кадру
        mmx_array<int>  statHistW/*(256)*/; // гистограмма цветов*ширину
        mmx_array<int>  statHistC/*(256)*/; // гистограмма цветов в контуре
        mmx_array<short> iLine/*(w)*/;      // округленный цвет

    }TMP_AURA_STAT;
public:
    CVIEngineBase *     m_pBase; // Ссылка на общий родительский инстанс класса реализованных алгоритмов программы
    CVIEngineConfig*    m_pCfg; // Ссылка на общий инстанс класса настроечных параметров (является членом класса CVIEngineBase)

    HANDLE              m_hThread; // Дескриптор потока (указатель на поток), в соответствии со способом идентмфикации параллельных потоков Windows
    DWORD               m_nId; // Номер параллельного потока
    std::vector<int>    m_nSum;
public:
    CVIEngineEvent  m_events[EVI_CNT];
    CVIEngineEvent  m_evReady;
public:
    int             m_yS,m_yE; // Для определения диапазона обрабатываемых строк изображения инстансом данного класса
public:
    std::vector<TMP_STAT>   m_stat; // Статистики по вычисленному дельта-изображению (имеется ввиду только статистики того фрагмента изображения который обрабатывал инстанс данного класса)
    mmx_array2<float>       m_tmp; // Временный двумерный массив размещаемый в видео памяти
    TMP_AURA_STAT           m_tmp_aura;
public:
    CVIEngineThread(CVIEngineBase *pBase);
    ~CVIEngineThread(void);
protected:
    static DWORD WINAPI ThreadProc(  LPVOID lpParameter );
    virtual void    Main();
public:
    virtual bool Start(void);
    virtual void Stop(void);
protected:
    void OnEventAddF(void);
    void OnEventSumm(void);
    void OnEventResult(void);
    void OnEventDelta(void);
    void OnEventAura(void);
    void OnEventSumStat(void);
    void OnEventSumFilter(void);


    void MakeSummPtr(int n,SUMM_PTR* pSumm);
public:
    int round(float v);
public:
    static int res2n(int res);
    static bool IsModeA(int res);
    static bool IsModeB(int res);
public:

    void Init(void);
    void ClearStat(void);
    void MakeResultSrc(int res);
    void MakeResultSrc8(int res);
    void MakeResultSrc24(int res);
    void MakeResultVI(int res);
    void MakeResultDelta(int res);
    void FilterSP(float *p);
    void FilterSPsse(float *p);
    void MakeAuraV6(int nSum, bool bModeB);

    void MakeAuraStat(int nSum, bool bModeB);
    float* GetSumPtr(int nSum,bool bModeB);
    short* GetSumPtrI(int nSum,bool bModeB);

    void MakeResultAuraDraw(int n, DWORD*   imgRes, int rMode);
    void MakeResultAuraDrawMix(int n, DWORD*    imgRes, int rMode);
    void MakeResultVIDraw(int n, DWORD* imgRes, int rMode);

    void MakeSumStat(int nSum, SUMM_PTR& S);
    void MakeAuraStatHist(AURA_STAT &stat);
    void MakeAuraStatTransform(AURA_STAT &stat);
    int MakeAuraColorA(int nSum, int x, int y);
    void MakeIntResult(float * pf, short * pi, mmx_array<float>& hX, mmx_array<float>& hY, int w, int h);
};
/// <summary>
/// Функция преобразования числа с плавающей точкой в целое число
/// Округление производится до ближайшего целого
/// </summary>
/// <param name="v">Число с плавающей точкой</param>
inline int CVIEngineThread::round(float v)
{
    return (int)(v>=0? v+0.5f : v-0.5f);
}
/// <summary>
/// Признак режима B
/// Расчитывается как отрицание признака режима A
/// </summary>
/// <param name="res"></param>
inline bool CVIEngineThread::IsModeB(int res)
{
    return !IsModeA(res);
}
