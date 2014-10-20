#pragma once
/// <summary>
///  Класс реализации API для системы отображения, то есть дисплея.
/// </summary>
class CVIEngineFace
{
public:
	CVIEngineFace();
	~CVIEngineFace();
	void MakeRelease();
	void MakeStatRelease();
	void MakeDraw(RGBQUAD* imgRes, int w, int h);
	void MakeStatLine(int y, short *sp, int w);
};

