#pragma once
/// <summary>
/// Класс для работы с системными часами
/// </summary>
class CVITimer
{
public:
	CVITimer();
	~CVITimer();
	double Get(); // Нормализованное в секунды значение часов
};

/// <summary>
/// Класс для работы с системными часами
/// </summary>
class CVITimerSync
{
public:
	CVITimerSync();
	~CVITimerSync();
	bool Add(double t);
};
