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
/// Аналогичный проекит https://github.com/dprotopopov/MedianFilter
/// Описание алгоритма https://ru.wikipedia.org/wiki/Медианный_фильтр
/// Медиа́нный фи́льтр — один из видов цифровых фильтров, широко используемый в цифровой обработке сигналов
/// и изображений для уменьшения уровня шума. Медианный фильтр является нелинейным КИХ-фильтром.
/// Значения отсчётов внутри окна фильтра сортируются в порядке возрастания(убывания);
/// и значение, находящееся в середине упорядоченного списка, поступает на выход фильтра.
/// В случае четного числа отсчетов в окне выходное значение фильтра равно среднему значению двух отсчетов
/// в середине упорядоченного списка.
/// Окно перемещается вдоль фильтруемого сигнала и вычисления повторяются.
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
/// Для ускорения расчётов он создаёт несколько дочерних инстансов класса CVIEngineThread, выполняющих обсчёт отдельных фрагментов полного изображения в параллельных нитях на компьютере.
/// Количество создаваемых инстансов класса CVIEngineThread равно количеству процессоров на компьютере, но не более 8-ми.
/// В свою очередь выполнение методов данного класса осуществляется так же в одной из созданных нитей параллельных вычислений, то есть инстанс класса создаётся и управляется внешними механизмами программы как обычным параллельным потоком - например - через графический пользовательский интерфейс.
/// И управление запуском отдельных процедур, так же как у инстанса класса CVIEngineThread, осуществляется через модель событийных триггеров.
/// То есть кто-то или что-то или он сам поднимают флаг события. В цикле проверяется не поднят ли какой-нибудь флаг. Если какой-то флаг поднят то выполняется запрограммированное действие.
/// Помимо нитей для ускорения обработки расчётов по изображению, запускаются ещё 2 параллельные нити - одна с высоким приоритетом, а другая с обычным.
/// Первая нить отвечает за захват и визуализацию кадров изображения в реальном режиме времени.
/// Вторая нить отвечат за захват и расчёт медлено меняющихся параметров - то есть собственно расчётных параметров методики.
/// </summary>
class CVIEngineBase
{
	public:
	CVIEngineBase(int nThread);
	~CVIEngineBase(void);
	public:
		/// <summary>
		/// Указатель на функцию. Видимо относится к механизму хранения настроечных параметров. То есть управление хранением настроечных параметров не жёстко зашито в программу а позволяет использовать различные подключаемые модули к которым надо просто дописать одну фунцию и заполнить переменную указателем на неё.
		/// </summary>
		void(*CallbackOnNewVar)(void *pUserData, int id, int subID); 
		/// <summary>
		/// Указатель на данные, передаваемые и возвращаемы в-из функцию вызовом её через указатель CallbackOnNewVar
		/// </summary>
		void *CallbackOnNewVarData; 
		/// <summary>
		/// Указатель на функцию. Видимо относится к механизму захвата изображений с устройств. То есть получение видеоизображений не жёстко зашито в программу а позволяет использовать различные подключаемые модули к которым надо просто дописать одну фунцию и заполнить переменную указателем на неё. В доступном коде вызов функции по указателю выполняется в методе AddImage данного класса.
		/// </summary>
		void(*CallbackOnImg8)(void *pUserData, BYTE* i8, int w, int h, double t); 
		/// <summary>
		/// Указатель на данные, передаваемые и возвращаемы в-из функцию вызовом её через указатель CallbackOnImg8
		/// </summary>
		void *CallbackOnImg8Data; 
	public:
		/// <summary>
		/// Интерфейс для управление значениями настроечных параметров алгоритмов программы (чтение-запись)
		/// </summary>
		CVIEngineConfig m_cfg; 

		/// <summary>
		/// Видимо реализация API для управления камерой и микрофоном. Вполне возможно что автор сперва сделал полиграф для голоса, а для видео потом расписал. А название осталось.
		/// </summary>
		CVIEngineAudio2 m_audio; 

	public:
		/// <summary>
		/// </summary>
		int m_nThreadsRqst;
		/// <summary>
		/// Текущее количество паралельных потоков вычислений, т.е. текущее количество дочерних инстансов классов CVIEngineThread для управления потоками вычислений
		/// </summary>
		int m_nThreads; 
		/// <summary>
		/// </summary>
		CVIEngineThread* m_therads[8]; // Массив(стек) ссылок на инстансы классов для управления потоками вычислений
		/// <summary>
		/// Адресное пространство где находятся семафоры.
		/// Для семафора требуется участок памяти доступный из всех параллельных нитей.
		/// Управление семафором осуществляктся через класс CMTSingleLock.
		/// Статьи по теме http://msdn.microsoft.com/en-us/library/windows/desktop/ms682530(v=vs.85).aspx
		/// </summary>
		CMTCriticalSection m_locks[LVI_CNT];
		/// <summary>
		/// Массив программно управляемых событийных триггеров. Размер массива равен количеству всех возможных событий, которые могут формироватся программой. m_events[id] - это инстанс триггера для события с идентификатором id.
		/// </summary>
		CVIEngineEvent m_events[EVI_CNT]; 


		/// <summary>
		/// Реализация работы с системными часами. Успользуется для замеров времени работы процедур обработки изображений с целью оптимизации производительности программы на конкретном компьютере.
		/// </summary>
		CVITimer m_timer; 
		/// <summary>
		/// Реализация работы с системными часами. Успользуется для замеров времени работы процедур обработки изображений с целью оптимизации производительности программы на конкретном компьютере.
		/// </summary>
		CVITimerSync m_timerSync;
	public:
		/// <summary>
		/// Отметка времени при обработке изображения кадра
		/// </summary>
		double m_tMakeImage;
		/// <summary>
		/// Отметка времени и длительность при обработке изображения кадра
		/// </summary>
		double m_tVideo, m_tVideoT; 
		/// <summary>
		/// Отметка времени и длительность при обработке изображения предыдущего кадра
		/// </summary>
		double m_tVideoPrev, m_tVideoTPrev; 
		/// <summary>
		/// </summary>
		double m_tVideoDT;
	public:
		/// <summary>
		/// </summary>
		std::list< SRC_IMG > m_srcF;
	public:
		/// <summary>
		/// Флаг режима инициализации программы
		/// </summary>
		bool m_bInit;
		/// <summary>
		/// Флаг режима остановки вычислений
		/// </summary>
		int m_bStop;
		/// <summary>
		/// Флаг режима завершения работы программы
		/// </summary>
		int m_bDone;
		/// <summary>
		/// </summary>
		int m_bLock;

		/// <summary>
		/// Элемент для устранения конфликтов в многопроцессорной среде.
		/// Используется в функции MakeImage().
		/// Способ применения: m_nMake ++; ... КОД ЗДЕСЬ ... ; m_nMake --;
		/// </summary>
		int m_nMake;
		/// <summary>
		/// Элемент для устранения конфликтов в многопроцессорной среде.
		/// Используется в функции Make().
		/// Способ применения: m_cMake[command] ++; ... КОД ЗДЕСЬ ... ; m_cMake[command] --;
		/// </summary>
		int m_cMake[EVI_CNT];
	public:
		/// <summary>
		/// </summary>
		float* m_imgSrcF;
		/// <summary>
		/// Двумерный массив монохромного изображения [0;255]
		/// </summary>
		mmx_array2<BYTE> m_imgSrc8; 
		/// <summary>
		/// Двумерный массив цветного изображения (RGB). 
		/// The RGBTRIPLE structure describes a color consisting of relative intensities of red, green, and blue. The bmciColors member of the BITMAPCOREINFO structure consists of an array of RGBTRIPLE structures.
		/// http://msdn.microsoft.com/en-us/library/windows/desktop/dd162939(v=vs.85).aspx
		/// </summary>
		mmx_array2<RGBTRIPLE> m_imgSrc24; 

		/// <summary>
		/// Двумерный массив чисел [0;255] монохромного изображения-маски, хранящий накладываемую на обрабатываемые исходные данные маску яркости пикселей
		/// </summary>
		mmx_array2<BYTE> m_imgSrcMask; 

		/// <summary>
		/// Массив (очередь) указателей на инстансы типа FRAME_IMG (способ хранения очереди исходных изображений)
		/// </summary>
		lFRAME_IMG m_arrSrc; 
		/// <summary>
		/// Массив (очередь) указателей на инстансы типа FRAME_IMG (способ хранения очереди расчитанных дельт-изображений)
		/// </summary>
		lFRAME_IMG m_arrDelta; 

		/// <summary>
		/// Друхмерный массив чисел с плавающей точкой монохромного изображения, хранящий накладываемую на обрабатываемые исходные данные маску яркости пикселей
		/// </summary>
		mmx_array2<float> m_srcMask; 

		/// <summary>
		/// Массив вычисленных различных изображений от различных обработчиков. По своей сути является картоы,
		/// поскольку структура SUM_IMG содержит аттрибут id == идентификатору обработчика.
		/// </summary>
		std::vector<SUM_IMG> m_summ;
		/// <summary>
		/// Массив вычисленных различных статистик от различных обработчиков.
		/// Данные в элементах массива формируются сведением данных от параллельных обработчиков фрагментов изображений.
		/// </summary>
		std::vector<SUMM_STAT> m_stat;
		/// <summary>
		/// Массив вычисленных различных статистик от различных обработчиков, предназначенный для отобоажения
		/// </summary>
		std::vector<SUMM_STAT> m_statRelease;
		/// <summary>
		/// </summary>
		CVIEngineProc2x m_stat2;

		/// <summary>
		/// </summary>
		std::vector<CVIEngineAura6> m_aura6A;
		/// <summary>
		/// </summary>
		std::vector<CVIEngineAura6> m_aura6B;

		/// <summary>
		/// Видимо относится к позиции вывода изображения на дисплей.
		/// </summary>
		CVIEngineVPos m_vPos;

		/// <summary>
		/// Видимо дополнительный алгоритм по расчёту параметров.
		/// Название напоминает  усреднение чего-то.
		/// </summary>
		CStatAVG_Pack m_statAVG;
		/// <summary>
		/// Видимо дополнительный алгоритм по расчёту параметров.
		/// Название напоминает Быстрое преобразование Фурье https://ru.wikipedia.org/wiki/Быстрое_преобразование_Фурье.
		/// Одно из подобного класса быстрых преобразований - преобразования Адамара https://github.com/dprotopopov/fwht 
		/// </summary>
		CStatFFTW_Pack m_statFFT;

#ifndef  SEQ_DISABLE_LD
		/// <summary>
		/// Видимо хук при добавлении параметра в настроечные параметры
		/// </summary>
		CStatLDF_Pack m_statLDF;
#endif

#ifndef  SEQ_DISABLE_FN
		/// <summary>
		/// Видимо хук при добавлении параметра в настроечные параметры
		/// </summary>
		CStatFN_Pack m_statFn;
#endif

		/// <summary>
		/// Используемая палитра цветов
		/// </summary>
		RGBQUAD __declspec(align(16)) m_palI[256];

		/// <summary>
		/// Буфер данных для применения медианного фильтра
		/// </summary>
		std::list<int> m_divMaker;
	public:
		/// <summary>
		/// Карта вычисленных изображений.
		/// Каждому виду отображаемого результата соответствут свой ключ.
		/// </summary>
		std::map<int, DWORD*> m_resultPtr;
		/// <summary>
		/// Видимо текущее значение размеров формируемого изображения для визуализации
		/// </summary>
		SIZE m_resultSize;
		/// <summary>
		/// </summary>
		DWORD m_resultVer;
	public:
		/// <summary>
		/// Аттрибут для определения частоты обработки кадров
		/// </summary>
		CStatFPS m_fpsIn;
		/// <summary>
		/// Аттрибут для определения частоты обработки кадров
		/// </summary>
		CStatFPS m_fpsOutF;
		/// <summary>
		/// Аттрибут для определения частоты обработки кадров
		/// </summary>
		CStatFPS m_fpsDropF;
		/// <summary>
		/// Аттрибут для определения частоты обработки кадров
		/// </summary>
		CStatFPS m_fpsOutR;
		/// <summary>
		/// Аттрибут для определения частоты обработки кадров
		/// </summary>
		CStatFPS m_fpsDropR;

		/// <summary>
		/// Инстанс на API манипулирования изображением
		/// </summary>
		CVIEngineCrop m_crop;
#ifndef  SEQ_DISABLE_DISTORTION
		/// <summary>
		/// Вероятнее всего инстанс класса библиотеки видеоэффектов.
		/// </summary>
		CVIEngineDistortion m_Distortion;
#endif  // #ifndef SEQ_DISABLE_DISTORTION

	public:
		/// <summary>
		/// </summary>
		CVIEngineProcDT m_procF6;
#ifndef  SEQ_LITE
		/// <summary>
		/// Указатель на инстанс API вывода на дисплей
		/// </summary>
		CVIEngineFace * m_pFace;
#endif

	public:
	static DWORD WINAPI AddImageThread( LPVOID lpParameter );
	void AddImageThreadLocal();
	int AddImageThreadProc();
	/// <summary>
	/// Дескриптор параллельной нити с обработчиком AddImageThreadLocal
	/// </summary>
	HANDLE m_hThreadAddImage; 
	static DWORD WINAPI AddImageThread8( LPVOID lpParameter );
	void AddImageThreadLocal8();
	/// <summary>
	/// Дескриптор параллельной нити с обработчиком AddImageThreadLocal8
	/// </summary>
	HANDLE m_hThreadAddImage8; 
	public:
	static int res2n(int res); // Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной
	static bool IsModeA(int res); // Вычисление признака работы в режиме A согласно списку допустимых значений переменной
	static bool IsModeB(int res); // Вычисление признака работы в режиме B согласно списку допустимых значений переменной
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
	/// <summary>
	/// Получение палитры цветов.
	/// В предоставненом коде описание отсутствует, вызовов есть только в кострукторе класса.
	/// </summary>
	void MakeDefPal(RGBQUAD* pal);
	/// <summary>
	/// В предоставненом коде описание отсутствует, вызовов метода нет.
	/// </summary>
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

	void MakeAnger(void); // Вызов одноимённого метода с параметрами для режима B и для режима A.
	void MakeStress(void); // Вызов одноимённого метода с параметрами для режима B и для режима A.
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

	void MakeFaceDraw(); // Вывод картинки на экран через API в классе CVIEngineFace.

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
/// Блокирование семафора LVI_ALL с ожиданием его освобождения другими процессами.
/// То есть после выполнения данного метода все дочерние процессы будут находится в одинаковом статусе и можно считать-записать данные во все из-в них.
/// Точная реализация механизмов семафоров в данной программе неизвестна, но не может сильно отличатся от стандартных механизмов Windows.
/// </summary>
/// <param name="res"></param>
inline void CVIEngineBase::Sync(void)
{
	CMTSingleLock lock(m_locks + LVI_ALL, true);
}

/// <summary>
/// Видимо хук при добавлении параметра в настроечные параметры
/// </summary>
/// <param name="id"></param>
/// <param name="subID"></param>
inline void CVIEngineBase::OnNewVar(int id, int subID)
{
#ifndef  SEQ_DISABLE_LD
	m_statLDF.OnNewVar(id);
#endif

#ifndef  SEQ_DISABLE_FN
	m_statFn.OnNewVar(id);
#endif

	if(id > VI_FILTER_DISABLE_START && id < VI_FILTER_DISABLE_END)
	OnNewVarDidable(id); // Проверка что настроечный параметр с указанным id установлен, и, в противном случае, выполнение операции Reset.

	if( CallbackOnNewVar )
	CallbackOnNewVar(CallbackOnNewVarData,id,subID);


}

/// <summary>
/// Вычисление признака работы в режимах 0,1 или 2 согласно списку допустимых значений переменной res.
/// Просто вызов одноимённой глобальной функции класса CVIEngineThread.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
inline int CVIEngineBase::res2n(int res)
{
	return CVIEngineThread::res2n(res);
}

/// <summary>
/// Вычисление признака работы в режиме A согласно списку допустимых значений переменной res
/// Просто вызов одноимённой глобальной функции класса CVIEngineThread.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
inline bool CVIEngineBase::IsModeA(int res)
{
	return CVIEngineThread::IsModeA(res);
}
/// <summary>
/// Вычисление признака работы в режиме B согласно списку допустимых значений переменной res
/// Просто вызов одноимённой глобальной функции класса CVIEngineThread.
/// </summary>
/// <param name="res">Набор двоичных флагов</param>
inline bool CVIEngineBase::IsModeB(int res)
{
	return CVIEngineThread::IsModeB(res); // Вычисление признака работы в режиме B согласно списку допустимых значений переменной
}

/// <summary>
/// Признак того чтобы пропустить обработку кадра.
/// Причины пропусков:
/// Программа работает по факту движения, то есть заданны настройки для сенсора движения;
/// Либо фильтры работают по большему количеству кадров чем сейчас считано. 
/// </summary>
inline bool CVIEngineBase::IsSkip(void)
{
	if(IsMotion()) // Чтение настройки для процедуры сенсора определения факта движения на основе ранее расчитанных статистических данных
	return true;
	if(m_cfg.GetI1(VI_VAR_NFRAME) < m_cfg.GetI1(VI_FILTER_NSKIP))
	return true;
	return false;
}

/// <summary>
/// Проверка значения настроечного параметра VI_FACE_ENABLE
/// Возвращает true если параметр VI_FACE_ENABLE установлен в ненулевое значение
/// </summary>
inline bool CVIEngineBase::NeedSrcImageProc()
{
	if(m_cfg.GetI1(VI_FACE_ENABLE ))
	return true;
	return false;
}
