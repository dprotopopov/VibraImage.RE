#pragma once
enum
{
	EVI_DONE,
	EVI_ADD8_READY,
	EVI_ADD8,
	EVI_ADDF,
	EVI_DELTA,
	EVI_SUM,
	EVI_SUM_FILTER,
	EVI_SUM_STAT,
	EVI_AURA,
	EVI_RESULT,
	EVI_CNT // ���������� ��������� ������� == ������� ������� �������� � ������������
};
/// <summary>
/// ���������������� ������ ����� �������� ���������� ����������� ���������
/// </summary>
class CVIEngineEvent
{
public:
	HANDLE m_hEvent;
	CVIEngineEvent();
	~CVIEngineEvent();
	void Set();
	void Reset();
	void Wait(int waittime, HANDLE evt);
};

