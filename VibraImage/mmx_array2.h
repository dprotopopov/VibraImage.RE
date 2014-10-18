#pragma once
/// <summary>
/// Структура для хранения двухмерных массивов заданного типа данных
/// Вероятно оптимизирован для работы с видеопамятью по технологии MMX или  SSE
/// </summary>
template <class T> class mmx_array2 
{
public:
	/// <summary>
	/// Указатель на начало последавательности элементов в массиве
	/// </summary>
	T *p; 
	/// <summary>
	/// Указатель на конец последавательности элементов в массиве
	/// </summary>
	T *s; 
	/// <summary>
	/// Ширина
	/// </summary>
	int w; 
	/// <summary>
	/// Высота
	/// </summary>
	int h; 
	mmx_array2();
	~mmx_array2();
	void resize(int width, int height, bool);
	bool empty();
	void clear();
	voiv set(T * begin, T value, T * end);
};

