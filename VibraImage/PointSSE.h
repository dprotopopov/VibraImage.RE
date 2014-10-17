﻿#pragma once
/// <summary>
/// Структура для работы с пикселем в видеопамяти по технологии SSE.
/// Каждый пискель в видеопамяти является блоком из 4-х регистров для обработки 4-х каналов сигнала RGBA.
/// Которые могут записыватся и считыватся в-из видеопамять из-в ОЗУ как целые числа и как числа с плавающей точкой.
/// Благодаря этой возможности видеопамяти применяют аппаратное ускорение вычислений при преобразовании типов данных из float в int и обратно, либо при выполнении сложения-умножения.
/// То есть достаточно записать в регистры видеопамятм 4 числа с плавающей точкой, а потом считать эти же 4 числа как целые.
/// Реальный способ представления данных в видеопамяти определяет производитель оборудования, и как правило это хранение мантисы из диапазона [0.0;1.0].
/// При этом одновременно при чтении-запииси может производится умножение-деление на некоторую величину хранящуюся в системном регистре видеокарты.
/// Технология SSE уже является устаревшей. Современные видеокарты NVIDIA поддерживают технологию CUDA, позволяющей создавать полноценные программы для параллельных вычислений на видеокартах.
/// Однако стоимость вычислений и возможность параллельных вычислений не всегда находятся в прямой зависимости друкг от друга.
/// Дорогой процессор будет всегда делать вычисления быстрее дешовой видеокарты, сколько бы там шредеров не было - чудес не бывает.
/// </summary>

class CPointSSE
{
public:
	CPointSSE();
	CPointSSE(float value); 
	~CPointSSE();
	void set0();
	void limit_hi(CPointSSE value);
	void export2c(float *ph); 
	void operator = (float *ph); 
	void operator += (CPointSSE p); 
	float sum(); 
};
