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
		/// <summary>
		/// </summary>
		int n, nLast;

		/// <summary>
		/// </summary>
		SUMM_STAT * pStat;

		/// <summary>
		/// </summary>
		float *pSummA, *pSummB;
		/// <summary>
		/// </summary>
		float *pCSummA, *pCSummB;
		/// <summary>
		/// </summary>
		float *pDeltaA;
		/// <summary>
		/// </summary>
		float *pCDeltaA;

		/// <summary>
		/// </summary>
		float *pRetA, *pRetB;
		/// <summary>
		/// </summary>
		float *pCRetA, *pCRetB;

		/// <summary>
		/// </summary>
		short *pRetIA, *pRetIB;
		/// <summary>
		/// </summary>
		short *pCRetIA, *pCRetIB;


		/// <summary>
		/// </summary>
		float   divA;
		/// <summary>
		/// </summary>
		float   divB;
    } SUMM_PTR;

	/// <summary>
	/// Структура для хранения статистики по вычисленному фрагменту дельта-изображения для одного процесса
	/// </summary>
	typedef struct tagTMP_STAT
    {
		/// <summary>
		/// </summary>
		mmx_array2<float>   sumN8A;
		/// <summary>
		/// </summary>
		mmx_array2<float>   sumN8B;
		/// <summary>
		/// </summary>
		mmx_array<float>    sumN8XA;
		/// <summary>
		/// </summary>
		mmx_array<float>    sumN8XB;
		/// <summary>
		/// </summary>
		mmx_array<float>    sumN8YA;
		/// <summary>
		/// </summary>
		mmx_array<float>    sumN8YB;

		/// <summary>
		/// Cумма значений яркости всех пикселей в дельта-изображении после применения фильтров подавления шумов.
		/// </summary>
		float               dsumA; 
		/// <summary>
		/// Количество пикселей с ненулевыми значениями яркости в дельта-изображении после применения фильтров подавления шумов.
		/// </summary>
		float               dsumB; 
		/// <summary>
		/// </summary>
		float               sumAi, sumBi;

		/// <summary>
		/// </summary>
		float               cntAi, cntBi;

    } TMP_STAT;


    typedef struct tagTMP_AURA_STAT
    {
		/// <summary>
		/// гистограмма цветов
		/// </summary>
		mmx_array<int>  statHist/*(256)*/;  
		/// <summary>
		/// гистограмма цветов по кадру
		/// </summary>
		mmx_array<int>  statHistA/*(256)*/; 
		/// <summary>
		/// гистограмма цветов*ширину
		/// </summary>
		mmx_array<int>  statHistW/*(256)*/; 
		/// <summary>
		/// гистограмма цветов в контуре
		/// </summary>
		mmx_array<int>  statHistC/*(256)*/; 
		/// <summary>
		/// округленный цвет
		/// </summary>
		mmx_array<short> iLine/*(w)*/;      

    }TMP_AURA_STAT;
public:
	/// <summary>
	/// Ссылка на общий родительский инстанс класса реализованных алгоритмов программы
	/// </summary>
	CVIEngineBase *     m_pBase; 
	/// <summary>
	/// Ссылка на общий инстанс класса настроечных параметров (является членом класса CVIEngineBase)
	/// </summary>
	CVIEngineConfig*    m_pCfg; 

	/// <summary>
	/// Дескриптор потока (указатель на поток), в соответствии со способом идентмфикации параллельных потоков Windows
	/// </summary>
	HANDLE              m_hThread; 
	/// <summary>
	/// Номер параллельного потока
	/// </summary>
	DWORD               m_nId; 
	/// <summary>
	/// </summary>
	std::vector<int>    m_nSum;
public:
	/// <summary>
	/// </summary>
	CVIEngineEvent  m_events[EVI_CNT];
	/// <summary>
	/// </summary>
	CVIEngineEvent  m_evReady;
public:
	/// <summary>
	/// Для определения диапазона обрабатываемых строк изображения инстансом данного класса
	/// </summary>
	int             m_yS, m_yE; 
public:
	/// <summary>
	/// Статистики по вычисленному дельта-изображению (имеется ввиду только статистики того фрагмента изображения который обрабатывал инстанс данного класса)
	/// </summary>
	std::vector<TMP_STAT>   m_stat; 
	/// <summary>
	/// Временные данные. Временный двумерный массив размещаемый в видео памяти
	/// </summary>
	mmx_array2<float>       m_tmp;
	/// <summary>
	/// Временные данные.
	/// </summary>
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
    static int res2n(int res); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной
	static bool IsModeA(int res); // Вычисление признака работы в режиме A согласно списку допустимых значений переменной
    static bool IsModeB(int res); // Вычисление признака работы в режиме B согласно списку допустимых значений переменной
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
/// Вычисление признака работы в режиме B согласно списку допустимых значений переменной res
/// Расчитывается как отрицание признака режима A
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
inline bool CVIEngineThread::IsModeB(int res)
{
    return !IsModeA(res);
}
