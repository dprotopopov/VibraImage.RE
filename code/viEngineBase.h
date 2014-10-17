#pragma  once

#include  "vievents.h"
#include  "VIEngineThread.h"
#include  "viengineconfig.h"
#include  ".\fpsstat.h"
#include  ".\viengineproc2x.h"
#include  ".\statavg_pack.h"
#include  ".\viengineaudio2.h"
#include  ".\viengineaudio.h"
#include  ".\statfftw_pack.h"
#include  ".\vienginestat.h"
#include  "vi_timer.h"
#include  "vi_timer_sync.h"
#include  "viEngine_types.h"
#include  ".\guardantcheck7.h"
#include  "VIEngineAura6.h"
#include  "VIEngineVPos.h"
#include  "StatFN_Pack.h"
#include  "VIEngineCrop.h"
#include  "VIEngineProcDT.h"
#include  "VIEngineFace.h"
#include  "VIEngineDistortion.h"

/// <summary>
/// Процедура, применяемая в медианном фильтре при обработке изображения
/// https://github.com/dprotopopov/MedianFilter
/// https://ru.wikipedia.org/wiki/Медианный_фильтр
/// Медиа́нный фи́льтр — один из видов цифровых фильтров, широко используемый в цифровой обработке сигналов и изображений для уменьшения уровня шума. Медианный фильтр является нелинейным КИХ-фильтром.
/// Значения отсчётов внутри окна фильтра сортируются в порядке возрастания(убывания); и значение, находящееся в середине упорядоченного списка, поступает на выход фильтра.В случае четного числа отсчетов в окне выходное значение фильтра равно среднему значению двух отсчетов в середине упорядоченного списка.Окно перемещается вдоль фильтруемого сигнала и вычисления повторяются.
/// То есть для каждого пикселя, для каждого цветового слоя изображения формируется массивы значений весов цветов
/// Затем эти массивы сортируются и процедура возвращает значение из середины массива (среднее значение двух элементов в середине массиве если размер массива чётный)
/// Дананный шаблон функции является процедурой обработки одного такого массива для абстрактного типа T
/// Массив передаётся как струкрура списка для типа T
/// </summary>
/// <param name="l">Массив(список) значений</param>
/// <param name="limit">Количество элементов в массиве(списке) - не испльзуется</param>
template <class T>
inline T Median(std::list<T>& l,size_t limit)
{
	while(l.size() > limit)
	l.pop_front();
	if(l.empty())
	return 0;

	std::list<T> tmp = l;
	tmp.sort();
	while(tmp.size() > 2)
	{
		tmp.pop_back();
		tmp.pop_front();
}
return (tmp.front()+tmp.back())/2;
}

/// <summary>
/// Базовый класс для описания алгоритмов программы.
/// Поддерживает выполнение расчётов в многопоточной среде.
/// В программе создаётся только один инстанс данного класса.
/// </summary>
class CVIEngineBase
{
	public:
	CVIEngineBase(int nThread);
	~CVIEngineBase(void);
	public:
	void (*CallbackOnNewVar)(void *pUserData,int id,int subID);
	void *CallbackOnNewVarData;
	void (*CallbackOnImg8)(void *pUserData,BYTE* i8,int w,int h,double t);
	void *CallbackOnImg8Data;
	public:
	CVIEngineConfig m_cfg; // Значения настроечных параметров алгоритмов программы

	CVIEngineAudio2 m_audio;

	public:
	int m_nThreadsRqst;
	int m_nThreads; // Текущее количество паралельных потоков вычислений, т.е. текущее количество инстансов классов CVIEngineThread для управления потоками вычислений
	CVIEngineThread* m_therads[8]; // Массив(стек) ссылок на инстансы классов для управления потоками вычислений
	CMTCriticalSection m_locks[LVI_CNT];
	CVIEngineEvent m_events[EVI_CNT];


	CVITimer m_timer;
	CVITimerSync m_timerSync;
	public:
	double m_tMakeImage;
	double m_tVideo,m_tVideoT;
	double m_tVideoPrev,m_tVideoTPrev;
	double m_tVideoDT;
	public:
	std::list< SRC_IMG > m_srcF;
	public:
	bool m_bInit;
	int m_bStop;
	int m_bDone;
	int m_bLock;

	int m_nMake;
	int m_cMake[EVI_CNT];
	public:
	float* m_imgSrcF;
	mmx_array2<BYTE> m_imgSrc8; // двумерный массив монохромного изображения
	mmx_array2<RGBTRIPLE> m_imgSrc24; // двумерный массив цветного изображения (RGB)

	mmx_array2<BYTE> m_imgSrcMask; // двумерный массив монохромного изображения-маски

	lFRAME_IMG m_arrSrc;
	lFRAME_IMG m_arrDelta;

	mmx_array2<float> m_srcMask;

	std::vector<SUM_IMG> m_summ;
	std::vector<SUMM_STAT> m_stat;
	std::vector<SUMM_STAT> m_statRelease;
	CVIEngineProc2x m_stat2;

	std::vector<CVIEngineAura6> m_aura6A;
	std::vector<CVIEngineAura6> m_aura6B;

	CVIEngineVPos m_vPos;

	CStatAVG_Pack m_statAVG;
	CStatFFTW_Pack m_statFFT;

#ifndef  SEQ_DISABLE_LD
	CStatLDF_Pack m_statLDF;
#endif

#ifndef  SEQ_DISABLE_FN
	CStatFN_Pack m_statFn;
#endif

	RGBQUAD __declspec(align(16)) m_palI[256];

	std::list<int> m_divMaker;
	public:
	std::map<int,DWORD*> m_resultPtr;
	SIZE m_resultSize;
	DWORD m_resultVer;
	public:
	CStatFPS m_fpsIn;
	CStatFPS m_fpsOutF;
	CStatFPS m_fpsDropF;
	CStatFPS m_fpsOutR;
	CStatFPS m_fpsDropR;

	CVIEngineCrop m_crop;
#ifndef  SEQ_DISABLE_DISTORTION
	CVIEngineDistortion m_Distortion;
#endif  // #ifndef SEQ_DISABLE_DISTORTION

	public:
	CVIEngineProcDT m_procF6;
#ifndef  SEQ_LITE
	CVIEngineFace * m_pFace;
#endif

	public:
	static DWORD WINAPI AddImageThread( LPVOID lpParameter );
	void AddImageThreadLocal();
	int AddImageThreadProc();
	HANDLE m_hThreadAddImage;
	static DWORD WINAPI AddImageThread8( LPVOID lpParameter );
	void AddImageThreadLocal8();
	HANDLE m_hThreadAddImage8;
	public:
	static int res2n(int res);
	static bool IsModeA(int res);
	static bool IsModeB(int res);
	public:

	void CreateThreads(bool bLock=true);
	void CloseThreads(bool bLock=true);

	bool AddImage(void* p, int w, int h, int bpp,double t,int nRef=0);
	bool AddImage8();
	bool MakeImage(float* pFI, BYTE *pBI,int w, int h);

	bool SetSize(int w, int h, int cnt);
	void SetCount(int cnt,bool bLock=true);

	bool CheckFPSDIV();

	mmx_array2<float>& NextSrc(void);
	void WaitThreads(void);
	void Sync(void);

	void SetSummCount(int cnt, int *pN);
	int GetSummCount(int *pN);
	int GetSummCount(int pos);

	bool SetResultPtr(int id,void* ptr, int w, int h);
	bool GetResultPtr(int id,void ** ptr, int* pw, int* ph);
	void Make(int command);
	int GetOptimalThreadCount(void);
	void MakeStatSum(void);
	void ClearStat(void);
	void ClearStat(SUMM_STAT& S);
	void MakeDefPal(RGBQUAD* pal);
	void SetMode(int tag, int id);


	void SetRegistry(LPCTSTR group);

	bool GetSrcLine(int x, int y, float* px=0, float* py=0);
	bool GetSrcLine8(int x, int y, float* px=0, float* py=0);
	bool GetSumHist(int id, float* px=0, float* py=0);

	void OnNewVar(int id,int subID=0);
	void StatUpdate(void);
	void Stop(void);
	void Pause(bool bSet);
	void Reset(bool bReset=false);

	void MakeStatFS2(float* src, int sw, int sh);
	void FlushFPS(void);
	void Start(void);
	bool CheckNRqst(int w, int h);
	void MakeResultSrc();

	int GetStatHistN(int res, int* pHist256,float *pFPS);
	int GetStatHistC(int res, int* pHist256,float *pFPS);
	int GetStatHistF(int res, int* pHist256,float *pFPS);
	int GetStatHistFT(int res, int* pHist256,float *pDT);

	void MakeAnger(void);
	void MakeStress(void);
	void MakeAnger(bool bModeB);
	void MakeStress(bool bModeB);
	void MakeSin(void);

	void tmp_aura_draw(void);
	void tmp_aura_draw(int res);
	void OnNewVarDidable(int id);
	int IsMotion(bool bSet=false);
	void MakeMotion(void);
	bool IsSkip(void);
	float MakeState(void);
	float MakeStateMacro(void);
	float MakeState(float Ag, float St, float Tn);

	void MakeFaceDraw();

	float MakeCharming(int* pHist256, int len);
	float MakeEntropyX(int* pHist256, int len);
	float MakeEntropyH(int* pHist256, int len);
	float MakeEntropyD(int* pHist256, int len);
	float MakeEntropyS(int* pHist256, int len);

	float MakeComN(int* pHist256, int len);
	float MakeComS(AURA_STAT& aura);

	bool SrcMaskLoad(LPCWSTR file);
	bool SrcMaskSave(LPCWSTR file);
	bool SrcMaskReset();
	bool SrcMaskErase(int x,int y);

	bool GetAura(int * pCWL, int * pCWR, int * pCR, int * pCL, int nProc, bool bB);
	bool CanMakeHist(int id);

	bool NeedSrcImageProc();

	void NewSource();
};


/// <summary>
/// </summary>
/// <param name="res"></param>
inline void CVIEngineBase::Sync(void)
{
	CMTSingleLock lock(m_locks + LVI_ALL, true);
}

/// <summary>
/// </summary>
/// <param name="res"></param>
inline void CVIEngineBase::OnNewVar(int id, int subID)
{
#ifndef  SEQ_DISABLE_LD
	m_statLDF.OnNewVar(id);
#endif

#ifndef  SEQ_DISABLE_FN
	m_statFn.OnNewVar(id);
#endif

	if(id > VI_FILTER_DISABLE_START && id < VI_FILTER_DISABLE_END)
	OnNewVarDidable(id);

	if( CallbackOnNewVar )
	CallbackOnNewVar(CallbackOnNewVarData,id,subID);


}

/// <summary>
/// </summary>
/// <param name="res"></param>
inline int CVIEngineBase::res2n(int res)
{
	return CVIEngineThread::res2n(res);
}

/// <summary>
/// </summary>
/// <param name="res"></param>
inline bool CVIEngineBase::IsModeA(int res)
{
	return CVIEngineThread::IsModeA(res);
}
/// <summary>
/// </summary>
/// <param name="res"></param>
inline bool CVIEngineBase::IsModeB(int res)
{
	return CVIEngineThread::IsModeB(res);
}

/// <summary>
/// </summary>
inline bool CVIEngineBase::IsSkip(void)
{
	if(IsMotion())
	return true;
	if(m_cfg.GetI1(VI_VAR_NFRAME) < m_cfg.GetI1(VI_FILTER_NSKIP))
	return true;
	return false;
}

/// <summary>
/// Проверка значения настроесного параметра VI_FACE_ENABLE
/// Возвращает true если параметр VI_FACE_ENABLE установлен в ненулевое значение
/// </summary>
inline bool CVIEngineBase::NeedSrcImageProc()
{
	if(m_cfg.GetI1(VI_FACE_ENABLE ))
	return true;
	return false;
}
