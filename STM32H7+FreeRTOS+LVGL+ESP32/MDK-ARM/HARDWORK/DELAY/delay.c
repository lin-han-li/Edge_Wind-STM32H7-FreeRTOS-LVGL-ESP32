#include "DELAY/delay.h"
#include "stm32h7xx_hal.h"
#include "tim.h"

//微秒延时
void delay_us(uint16_t us)
{
		uint16_t differ = 0xffff-us-5;
		__HAL_TIM_SET_COUNTER(&htim16,differ);//设定TIM16计数器起始值
		HAL_TIM_Base_Start(&htim16);//启动定时器

		while(differ<0xffff-5)//判断
		{
			
		differ = __HAL_TIM_GET_COUNTER(&htim16);//查询计数器的计数值

		}
		HAL_TIM_Base_Stop(&htim16);//关闭定时器
}
			 

//毫秒延时
void delay_ms(uint16_t ms)
{
while(ms>=1)	
{
ms=ms-1;
delay_us(1000);
}	
	
}

































