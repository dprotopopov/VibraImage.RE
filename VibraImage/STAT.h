#pragma once
/// <summary>
/// Структура для хранения статистики по вычисленному дельта-изображению
/// </summary>
class SUMM_STAT {
public:
	float dsumA; // Cумма значений яркости всех пикселей в дельта-изображении после применения фильтров подавления шумов.
	float dsumB; // Количество пикселей с ненулевыми значениями яркости в дельта-изображении после применения фильтров подавления шумов.
	AURA_STAT auraA;
	AURA_STAT auraB;
};
/// <summary>
/// Структура для хранения статистики по вычисленному дельта-изображению
/// </summary>
class AURA_STAT {
public:
	int imgCenterX;
	int transform;
	std::vector <AURA_STAT_LINE> line;
};

class AURA_STAT_LINE {
public:
	int xla, xra;
	int cl, cr;
	int cwl, cwr;
	int aColorL, aColorR;
	int maxLi, maxRi;
};