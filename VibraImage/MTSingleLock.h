#pragma once
/// <summary>
/// Данный класс является классом для работы со специальной памятью на которой реализуются семафоры.
/// Реализация семафоров в многопроцессорных системах является не тривиальной задачей и эти механизмы выносятся как правило в ядро ОС, а иногда и в архитектуру процессора.
/// Система Windows тоже содержит API для работы с CriticalSection и с семафорами.
/// Проблема заключается в том что операции работы с памятью в CriticalSection должны быть атомарными - то есть не могут быть выполнены одновременно двумя параллельными нитями.
/// Represents the access-control mechanism used in controlling access to a resource in a multithreaded program.
/// http://msdn.microsoft.com/en-us/library/bwk62eb7.aspx
/// </summary>
class CMTCriticalSection
{
public:
};
/// <summary>
/// Данный класс является классом для управления доступом к ресурсам в многопоточных приложениях
/// Represents the access-control mechanism used in controlling access to a resource in a multithreaded program.
/// http://msdn.microsoft.com/en-us/library/bwk62eb7.aspx
/// </summary>
class CMTSingleLock
{
public:
	CMTSingleLock(CMTCriticalSection nLocks, bool bLock);
	~CMTSingleLock();
	BOOL Lock();
	BOOL Unlock();
};

