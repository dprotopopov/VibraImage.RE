#include "PointSSE.h"


/// <summary>
/// Инициализация регистра видеопамяти
/// </summary>
CPointSSE::CPointSSE()
{
}
/// <summary>
// Инициализация регистра видеопамяти с указанным значением
/// </summary>
/// <param name="value">Значение для инициализации</param>
CPointSSE::CPointSSE(float value)
{
}


CPointSSE::~CPointSSE()
{
}

/// <summary>
/// Установка значений регистров видеопамяти в нулевое значение
/// </summary>
CPointSSE::set0()
{
}
/// <summary>
/// Задание верхнего уровня обрабатываемого сигнала
/// Исходный сигнал уровня яркости больше порогового значения считается шумом и подавляется (считается максимальной яркостью)
/// </summary>
/// <param name="value">Пороговое значение яркости</param>
CPointSSE::limit_hi(CPointSSE value)
{
}
/// <summary>
/// Задание нижнего уровня обрабатываемого сигнала
/// Исходный сигнал уровня яркости меньше порогового значения считается шумом и подавляется (считается минимальной яркостью)
/// </summary>
/// <param name="value">Пороговое значение яркости</param>
CPointSSE::limit_lo(CPointSSE value)
{
}
/// <summary>
/// Всем ненулевым значением в регистрах присваивается указанное значение
/// </summary>
/// <param name="value">Новое значение яркости у регистров с ненулевыми значениями</param>
CPointSSE::mask_nz(CPointSSE value)
{
}
/// <summary>
/// пересялка 16 байт из видеопамяти в память ОЗУ
/// </summary>
/// <param name="ph"></param>
CPointSSE::export2c(float *ph)
{
}
/// <summary>
/// Запись 16 байт из ОЗУ в видеопамять
/// </summary>
/// <param name="ph"></param>
CPointSSE::operator = (float *ph)
{
}
/// <summary>
/// Прибавление к данному блоку-пикселю из 4 регистров в видеопамяти значений из другого блока из 4-х регистров в видеопамяти
/// </summary>
/// <param name="p"></param>
CPointSSE::operator += (CPointSSE p)
{
}
/// <summary>
/// Суммирование значений 4-х регистров одного блока-пикселя и возврат результата в виде числа с плавающей точкой
/// </summary>
CPointSSE::sum()
{
}
