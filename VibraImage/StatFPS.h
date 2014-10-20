#pragma once
/// <summary>
/// Аттрибут для определения частоты обработки кадров
/// </summary>
class CStatFPS
{
public:
	CStatFPS();
	~CStatFPS();
	float Get();
	float Put(float value=0.0f);
};

