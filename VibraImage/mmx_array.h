#pragma once
/// <summary>
/// Структура для хранения одномерных массивов заданного типа данных
/// Вероятно оптимизирован для работы с видеопамятью по технологии MMX или  SSE
/// </summary>
template <class T> class mmx_array
{
public:
	/// <summary>
	/// Указатель на последавательность элементов в массиве
	/// </summary>
	T *p; 
	mmx_array();
	~mmx_array();
	void resize(int length, bool bInitItemsByDefaultValues = true);
	bool empty();
	void clear();
};

