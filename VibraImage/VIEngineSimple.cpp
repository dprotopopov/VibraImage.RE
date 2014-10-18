#include "VIEngineSimple.h"


CVIEngineSimple::CVIEngineSimple()
{
}


CVIEngineSimple::~CVIEngineSimple()
{
}

/// <summary>
/// Один из каких-то алгоритмов, включая алгоритмы подавления шумов с использованием возможностей технологии SSE под названием CT.
/// Реализаци¤ не известна, но не имеет никакого значения - можно выбрить любой фильтр подавлени¤ шумов.
/// </summary>
/// <param name="delta">Ѕлок-пиксель из 4-х регистров видеопам¤ти, содержащий фрагмент обрабатываемого дельта-сигнала</param>
/// <param name="pSrcF">”казатель на уровни ¤ркости текущей обрабатываемой строки исходного изображени¤</param>
/// <param name="w">Ўирина изображени¤</param>
/// <param name="filterCTf">ѕараметр алгоритма фильтра</param>
static void CVIEngineSimple::FilterCT(CPointSSE delta, float *pSrcF, int w, bool filterCTf)
{
}
