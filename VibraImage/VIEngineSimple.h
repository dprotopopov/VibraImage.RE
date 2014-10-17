#pragma once
/// <summary>
/// Класс реализации алгоритмов подавлени¤ шумов (фильтров) с использованием возможностей технологии SSE.
/// Реализаци¤ не известна, но не имеет никакого значени¤ - можно выбрить любой фильтр подавлени¤ шумов.
/// </summary>
class CVIEngineSimple
{
public:
	CVIEngineSimple();
	~CVIEngineSimple();
	static void FilterCT(CPointSSE delta, float *pSrcF, int w, bool filterCTf);
	static void RoundLine(float *p, short *sp, int w);
	static void AddHist(int *ip, short *sp, int w);
};

