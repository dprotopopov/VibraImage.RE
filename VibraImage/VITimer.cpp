#include "VITimer.h"


CVITimer::CVITimer()
{
}


CVITimer::~CVITimer()
{
}

/// <summary>
/// Hормализованное в секунды значение системных часов
/// </summary>
double CVITimer::Get()
{
}

CVITimerSync::CVITimerSync()
{
}


CVITimerSync::~CVITimerSync()
{
}

/// <summary>
/// Вызов в известном коде осуществляется только один раз - в процедуре чтения очередного изображения с устройства.
/// Предположительно это просто простановка отметки времени начала обработки очередного блока данных.
/// То есть просто сброс показаний секундомера или синхронизация с внутреннего таймера с системной микросхемой.
/// </summary>
/// <param name="t">Отметка времени</param>
double CVITimerSync::Add(double t)
{
}
