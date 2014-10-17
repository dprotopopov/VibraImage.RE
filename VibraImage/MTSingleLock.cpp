#include "MTSingleLock.h"


/// <summary>
/// Конструктор класса
/// </summary>
/// <param name="nLocks">Количество создаваемых семафоров</param>
/// <param name="bLock">Признак блокировать все семафоры при создании инстанса данного класса</param>
CMTSingleLock::CMTSingleLock(int nLocks, bool bLock)
{
}


CMTSingleLock::~CMTSingleLock()
{
}

/// <summary>
/// Блокирование одного семафора 
/// </summary>
BOOL CMTSingleLock::Lock()
{
}
/// <summary>
/// Разблокирование одного семафора 
/// </summary>
BOOL CMTSingleLock::Unlock()
{
}
