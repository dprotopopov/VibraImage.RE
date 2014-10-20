#include "VIEngineFace.h"


CVIEngineFace::CVIEngineFace()
{
}


CVIEngineFace::~CVIEngineFace()
{
}

/// <summary>
/// </summary>
void CVIEngineFace::MakeRelease()
{
}

/// <summary>
/// </summary>
void CVIEngineFace::MakeStatRelease()
{
}

/// <summary>
/// Вывод картинки на экран.
/// Картинка задаётся массивом значений RGB.
/// </summary>
/// <param name="imgRes">Массив значений RGB</param>
/// <param name="w">Ширина картинки</param>
/// <param name="h">Высота картинки</param>
void CVIEngineFace::MakeDraw(RGBQUAD* imgRes, int w, int h)
{
}

/// <summary>
/// Визуализация результатов вычислений на экране.
/// </summary>
/// <param name="y">Номер строки на экране</param>
/// <param name="sp">Массив значений уровней яркости в строке</param>
/// <param name="w">Ширина картинки</param>
void CVIEngineFace::MakeStatLine(int y, short *sp, int w)
{
}

