#include "VIEngineAudio2.h"


/// <summary>
/// Конструктор класса
/// </summary>
/// <param name="pBase">Указатель на инстанс класса CVIEngineBase. Этот инстанс единственный в работающей программе.</param>
CVIEngineAudio2::CVIEngineAudio2(CVIEngineBase *pBase) : m_pBase(pBase)
{
}


CVIEngineAudio2::~CVIEngineAudio2()
{
}

/// <summary>
/// Видимо не более чем процедура подготовки различных внутренних структур к готовности получать данные от устройства захвата звука или изображения.
/// </summary>
void CVIEngineAudio2::NewSource()
{
}
/// <summary>
/// Видимо просто запуск процедуры для копирования текущего кадра данных от медийного устройства в инстанс класса CVIEngineBase
/// </summary>
void CVIEngineAudio2::OnVideo()
{
}
