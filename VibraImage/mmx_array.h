#pragma once
/// <summary>
/// —труктура дл€ хранени€ одномерных массивов заданного типа данных
/// ¬еро€тно оптимизирован дл€ работы с видео-пам€тью
/// </summary>
template <class T> class mmx_array
{
public:
	mmx_array();
	~mmx_array();
	bool empty();
	void clear();
};

