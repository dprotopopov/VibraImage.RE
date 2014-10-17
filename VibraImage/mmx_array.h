#pragma once
/// <summary>
/// Структура для хранения одномерных массивов заданного типа данных
/// Вероятно оптимизирован для работы с видеопамятью
/// </summary>
template <class T> class mmx_array
{
public:
	T *p; // Указатель на последавательность элементов в массиве
	mmx_array();
	~mmx_array();
	void resize(int length, bool bInitItemsByDefaultValues = true);
	bool empty();
	void clear();
};

