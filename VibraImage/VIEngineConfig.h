#pragma once
/// <summary>
/// Список настроечных параметров, используемых в программе
/// </summary>
enum
{
	VI_FACE_ENABLE,
	VI_MODE_RESULT,
	VI_MODE_WBG, // Отображать результаты на белом фоне, иначе бэк чёрный
	VI_VAR_NFRAME,
	VI_VAR_FPSMAXF,
	VI_VAR_FPSMAXR
	VI_VAR_NFRAME_IN,
	VI_VAR_TH,
	VI_VAR_STAT_INTEGR0A,
	VI_VAR_STAT_INTEGR0B,
	VI_VAR_STAT_INTEGR1A,
	VI_VAR_STAT_INTEGR1B,
	VI_VAR_STAT_INTEGR2A,
	VI_VAR_STAT_INTEGR2B,
	VI_VAR_STAT_RES_A1,
	VI_VAR_STAT_RES_A1X,
	VI_VAR_STAT_RES_A2,
	VI_VAR_STAT_RES_A3,
	VI_VAR_STAT_RES_F1,
	VI_VAR_STAT_RES_F1X,
	VI_VAR_STAT_RES_F2,
	VI_VAR_STAT_RES_F3,
	VI_VAR_STAT_RES_F4,
	VI_VAR_STAT_RES_F5,
	VI_VAR_STAT_RES_F6,
	VI_VAR_STAT_RES_F7,
	VI_VAR_STAT_RES_F8,
	VI_VAR_STAT_RES_F9,
	VI_VAR_STAT_RES_S1,
	VI_VAR_STAT_RES_S2,
	VI_VAR_STAT_RES_S3,
	VI_VAR_STAT_RES_S4,
	VI_VAR_STAT_RES_S5,
	VI_VAR_STAT_RES_S6,
	VI_VAR_STAT_RES_S7,
	VI_VAR_STAT_RES_P1,
	VI_VAR_STAT_RES_P2,
	VI_VAR_STAT_RES_P3,
	VI_VAR_STAT_RES_P4,
	VI_VAR_STAT_RES_P9A,
	VI_VAR_STAT_RES_P9F,
	VI_VAR_STAT_RES_P10A,
	VI_VAR_STAT_RES_P10F,
	VI_VAR_STAT_RES_P11A,
	VI_VAR_STAT_RES_P11F,
	VI_VAR_STAT_RES_P12A,
	VI_VAR_STAT_RES_P12F,
	VI_VAR_STAT_CFG_SIN,
	VI_VAR_STAT_RES_SIN,
	VI_FILTER_AM,
	VI_FILTER_SP,
	VI_FILTER_CT, // Параметр неизвестного алгоритма подавления шумов (фильтра) при расчёте дельта-изображения
	VI_FILTER_DISABLE_A,
	VI_FILTER_DISABLE_B,
	VI_FILTER_NSKIP,
	VI_FILTER_PAUSE, // Флаг приостановки работы фильтров
	VI_FILTER_FPSDIV,
	VI_FILTER_FPSDIV_RQST,
	VI_FILTER_FPS2IN,
	VI_FILTER_AUTODOWNRATE,
	VI_FILTER_HISTNW,
	VI_FILTER_DISABLE_2X,
	VI_FILTER_DISABLE_VI0,
	VI_FILTER_DISABLE_VI1,
	VI_FILTER_DISABLE_VI2,
	VI_FILTER_DISABLE_VI3,
	VI_FILTER_DISABLE_VI4,
	VI_FILTER_MOTION,
	VI_FILTER_MOTION_SET,
	VI_FILTER_MOTION_LEVEL,
	VI_FILTER_MOTION_10X,
	VI_FILTER_MOTION_AUTO_RESET,
	VI_FILTER_DELTA_STRETCH, // Растягивать уровни яркости при расчёте дельта-изображения - дельта-сигнал преобразуется по формуле f(x) = (x - filterDeltaLO) * 255.0f/(255.0f - filterDeltaLO)
	VI_FILTER_DELTA_LO, // Нижний уровень яркости для подавления шумов при расчёте дельта-изображения
	VI_FILTER_BWT_F6_HI, 
	VI_FILTER_BWT_F6_LO, 
	VI_FILTER_F6_N
};
/// <summary>
/// Класс предназначен для управления списком настроечных параметров алгоритмов программы. 
/// В программе создаётся только один инстанс данного класса (реализован как член m_cfg класса CVIEngineBase).
/// </summary>
class CVIEngineConfig
{
public:
	CVIEngineConfig();
	~CVIEngineConfig();
	int GetI1(int key);
	float GetF1(int key);
	void GetI(int key, int &value1, int &value2);
	void PutI1(int key, int value);
	void PutF1(int key, float value);
};

