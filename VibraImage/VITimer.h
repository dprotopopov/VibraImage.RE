#pragma once
/// <summary>
/// Класс для работы с системными часами
/// </summary>
class CVITimer
{
public:
	CVITimer();
	~CVITimer();
	double Get(); // нормализованное в секунды значение системных часов
};

