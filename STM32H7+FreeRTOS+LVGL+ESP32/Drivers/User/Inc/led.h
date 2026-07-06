#ifndef __LED_H
#define __LED_H

/*------------------------------------------ LED���ú� ----------------------------------*/

#define LED1_PIN            			 GPIO_PIN_13        				 	// LED1 ����      
#define LED1_PORT           			 GPIOC                 			 	// LED1 GPIO�˿�     
#define __HAL_RCC_LED1_CLK_ENABLE    __HAL_RCC_GPIOC_CLK_ENABLE() 	// LED1 GPIO�˿�ʱ��
 

  
/*----------------------------------------- LED���ƺ� ----------------------------------*/
						
#define LED1_ON 	  	HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET)		// ����͵�ƽ������LED1	
#define LED1_OFF 	  	HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, GPIO_PIN_SET)			// ����ߵ�ƽ���ر�LED1	
#define LED1_Toggle	HAL_GPIO_TogglePin(LED1_PORT,LED1_PIN);							// ��תIO��״̬
			
/*---------------------------------------- �������� ------------------------------------*/

typedef enum {
    LED_MODE_NORMAL = 0,     // Normal: slow blink 1Hz (500ms)
    LED_MODE_WARNING,        // Warning: fast blink 2Hz (250ms)
    LED_MODE_CRITICAL,       // Critical: rapid blink 4Hz (125ms)
    LED_MODE_EMERGENCY       // Emergency: constant on
} LED_Mode_t;

void LED_Init(void);
void LED_SetMode(LED_Mode_t mode);
LED_Mode_t LED_GetMode(void);

#endif //__LED_H


