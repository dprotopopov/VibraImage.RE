#include "VIEngineConfig.h"


CVIEngineConfig::CVIEngineConfig()
{
}


CVIEngineConfig::~CVIEngineConfig()
{
}

/// <summary>
/// Чтение первого значения настроечного параметра по идентификатору ключа
/// Возвращаемый тип - целое
/// </summary>
/// <param name="key">Идентификатор ключа</param>
int CVIEngineConfig::GetI1(int key)
{
}
/// <summary>
/// Чтение второго значения настроечного параметра по идентификатору ключа
/// Возвращаемый тип - целое
/// </summary>
/// <param name="key">Идентификатор ключа</param>
int CVIEngineConfig::GetI2(int key)
{
}
/// <summary>
/// Чтение настроечного параметра по идентификатору ключа
/// Возвращаемый тип - число с плавающей точкой
/// </summary>
/// <param name="key">Идентификатор ключа</param>
float CVIEngineConfig::GetF1(int key)
{
}
/// <summary>
/// Чтение настроечного параметра по идентификатору ключа
/// Возвращаемый тип - пара значений целых, возвращаемая в переменные переданные по ссылкам в процедуру
/// Возвращает true если настроечный параметр задан,
/// fase в противном случае.
/// </summary>
/// <param name="key">Идентификатор ключа</param>
/// <param name="value1">Ссылка на первую переменную</param>
/// <param name="value2">Ссылка на вторую переменную</param>
bool CVIEngineConfig::GetI(int key, int &value1, int &value2)
{
}
/// <summary>
/// Сохранение настроечного параметра по идентификатору ключа
/// Сохраняемый тип - целое
/// </summary>
/// <param name="key">Идентификатор ключа</param>
/// <param name="value">Сохраняемое значение</param>
void CVIEngineConfig::PutI1(int key, int value)
{
}
/// <summary>
/// Сохранение настроечного параметра по идентификатору ключа
/// Сохраняемый тип - число с плавающей точкой
/// </summary>
/// <param name="key">Идентификатор ключа</param>
/// <param name="value">Сохраняемое значение</param>
void CVIEngineConfig::PutF1(int key, float value)
{
}
/// <summary>
/// Сохранение текущих значений настроечных параметров.
/// Видимо в регистре Windows, а может в файле - это видимо можно регулировать использованием разных плагинов.
/// </summary>
void CVIEngineConfig::RegSave()
{
}
/// <summary>
/// Задание идентификатора группы ключей в реестре Windows
/// </summary>
/// <param name="group">Идентификатор группы ключей в реестре Windows</param>
float CVIEngineConfig::SetRegistry(LPCTSTR group)
}
