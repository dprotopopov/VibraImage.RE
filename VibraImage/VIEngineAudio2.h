#pragma once
/// <summary>
/// Видимо реализация API для управления камерой и микрофоном. 
/// Вполне возможно что автор сперва сделал полиграф для голоса, а для видео потом расписал. 
/// А название осталось.
/// Вероятнее всего просто запускается параллельная нить в Windows? которая отвечает за захват изображения и звука через стандартные API мультимедиа Windows,
/// и выдаёт результат в программу по запросам от программы.
/// </summary>
class CVIEngineAudio2
{
protected:
	/// <summary> 
	/// Указатель на инстанс класса CVIEngineBase. Этот инстанс такого класса единственный в работающей программе.
	/// </summary>
	CVIEngineBase *m_pBase;
public:
	CVIEngineAudio2(CVIEngineBase *pBase);
	~CVIEngineAudio2();
	/// <summary>
	/// Флаг завершения работы процесса
	/// </summary>
	bool m_bDone; 
	/// <summary>
	/// Дескриптор нити Windows отвечающей за чтения данных с устройства
	/// </summary>
	HANDLE m_hThread; 
	void NewSource();
	void OnVideo();
};

