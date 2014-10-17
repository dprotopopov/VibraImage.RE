#pragma once
/// <summary>
/// Структура для хранения одномерных массивов заданного типа данных
/// Вероятно оптимизирован для работы с видео-памятью
/// </summary>
template <class T> class mmx_array
{
public:
	mmx_array();
	~mmx_array();
	bool empty();
	void clear();
};

