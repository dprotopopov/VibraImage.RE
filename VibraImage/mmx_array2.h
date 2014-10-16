#pragma once
/// <summary>
/// ��������� ��� �������� ���������� �������� ��������� ���� ������
/// �������� ������������� ��� ������ � �����-�������
/// </summary>
template <class T> class mmx_array2 : public mmx_array< mmx_array< T > >
{
public:
	int w; // ������
	int h; // ������
	mmx_array2();
	~mmx_array2();
	void resize(int width, int height, bool);
	bool empty();
	void clear();
};

