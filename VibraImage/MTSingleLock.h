#pragma once
/// <summary>
/// Данный класс является классом для управления доступом к ресурсам в многопоточных приложениях
/// Represents the access-control mechanism used in controlling access to a resource in a multithreaded program.
/// http://msdn.microsoft.com/en-us/library/bwk62eb7.aspx
/// </summary>
class CMTSingleLock
{
public:
	CMTSingleLock(int nLocks, bool bLock);
	~CMTSingleLock();
	BOOL Lock();
	BOOL Unlock();
};

