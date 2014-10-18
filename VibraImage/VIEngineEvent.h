#pragma once
/// <summary>
/// Список возможных событий, создаваемых и используемых в программе
/// </summary>
enum
{
	EVI_DONE, // Сигнал к завершению работы процесса
	EVI_ADD8, // Сигнал запроса следующего кадра для обработки. обрабатываемый самим базовым классам алгоритмов CVIEngineBase
	EVI_ADD8_READY, // Сигнал готовности кадра для обработки. обрабатываемый самим базовым классам алгоритмов CVIEngineBase
	EVI_ADDF,
	EVI_DELTA,
	EVI_SUM,
	EVI_SUM_FILTER,
	EVI_SUM_STAT,
	EVI_AURA,
	EVI_RESULT, // Сигнал готовности процесса продолжить работу
	EVI_CNT // Количество возможный событий == индексу данного элемента в перечислении
};
/// <summary>
/// Предположительно данный класс является программно управляемым событийным триггером
/// </summary>
class CVIEngineEvent
{
public:
	/// <summary>
	/// Дескриптор события
	/// </summary>
	HANDLE m_hEvent;
	CVIEngineEvent();
	~CVIEngineEvent();
	void Set();
	void Reset();
	void Wait(int waittime, HANDLE evt);
};

