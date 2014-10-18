#pragma once
/// <summary>
/// Структура для хранения статистики по вычисленному дельта-изображению
/// </summary>
class SUMM_STAT {
public:
	/// <summary>
	/// </summary>
	float dsumA; // Cумма значений яркости всех пикселей в дельта-изображении после применения фильтров подавления шумов.
	/// <summary>
	/// </summary>
	float dsumB; // Количество пикселей с ненулевыми значениями яркости в дельта-изображении после применения фильтров подавления шумов.
	/// <summary>
	/// </summary>
	AURA_STAT auraA;
	/// <summary>
	/// </summary>
	AURA_STAT auraB;
};
/// <summary>
/// Структура для хранения статистики по вычисленному дельта-изображению
/// </summary>
class AURA_STAT {
public:
	/// <summary>
	/// </summary>
	int imgCenterX;
	/// <summary>
	/// </summary>
	int transform;
	std::vector <AURA_STAT_LINE> line;
};

/// <summary>
/// Структура для хранения информации о линии ауры, рисуемой поверх основного изображения
/// </summary>
class AURA_STAT_LINE {
public:
	/// <summary>
	/// </summary>
	int xla, xra;
	/// <summary>
	/// </summary>
	int cl, cr;
	/// <summary>
	/// </summary>
	int cwl, cwr;
	/// <summary>
	/// </summary>
	int aColorL, aColorR;
	/// <summary>
	/// </summary>
	int maxLi, maxRi;
};