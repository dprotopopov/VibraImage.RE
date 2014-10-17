#pragma once
/// <summary>
/// Структура для хранения двухмерных массивов заданного типа данных
/// Вероятно оптимизирован для работы с видеопамятью
/// </summary>
template <class T> class mmx_array2 
{
public:
	T *p; // Указатель на последавательность элементов в массиве
	int w; // Ширина
	int h; // Высота
	mmx_array2();
	~mmx_array2();
	void resize(int width, int height, bool);
	bool empty();
	void clear();
};

