/*
 * obc.c
 *
 *  Created on: Nov 9, 2022
 *      Author: Nil Rius
 */
#include "Subsystems/obc.h"

void OBC_Init()
{
	uint8_t currentState; // OBC state : INIT, NOMINAL, CONTINGENCY, SUNSAFE OR SURVIVAL.
	uint8_t *deploy = 0; // Boolean for deployment. True if deployed. False otherwise.

	xTaskNotify(ADCS_Handle, DETUMBLING_NOTI, eSetBits); // Stabilize the satellite.
														 // OBC TASK -> ADCS TASK.

	xTaskNotify(EPS_Handle, EPS_BATTERY_NOTI, eSetBits); // Read battery in order to know if we have enough capacity for antenna deployment.
														 // EPS task updates the current state according to the battery capacity.
														 // OBC TASK -> EPS TASK.

	Read_Flash(CURRENT_STATE_ADDR, &currentState, 1); // Read the current state of the flash.

	if(currentState == _NOMINAL) // Deploy antennas only if the next state is NOMINAL.
	{
		xTaskNotify(COMMS_Handle, ANTENNA_DEPLOYMENT_NOTI, eSetBits); // Deploy COMMS antenna.
																	  // OBC TASK -> COMMS TASK.

		// xTaskNotify(RFI_Handle, ANTENNA_DEPLOYMENT_NOTI, eSetBits); // Deploy RF antenna.
																	   // OBC TASK -> RFI TASK.
		*deploy = 1;
	}

	Send_to_WFQueue(deploy, 1, ANTENNA_DEPLOYED_ADDR, OBCsender); // Save to the memory flash the current deployment state
																   // OBC TASK -> FLASH TASK

	xTaskNotify(ADCS_Handle, CHECKROTATE_NOTI, eSetBits); // Poll lateral temperature sensors and determine if rotation is needed
														  // OBC TASK -> ADCS TASK

	xTaskNotify(EPS_Handle, EPS_BATTERY_NOTI, eSetBits); // Read battery in order to know if we have enough capacity for antenna deployment.
	 	 	 	 	 	 	 	 	 	 	 	 	 	 // EPS task updates the current state according to the battery capacity.
	 	 	 	 	 	 	 	 	 	 	 	 	 	 // OBC TASK -> EPS TASK.
}

void OBC_Nominal()
{
	uint32_t CPUFreq;
	//uint8_t currentState, previousState;
	uint8_t deploy;


	CPUFreq = HAL_RCC_GetHCLKFreq(); // Read the current CPU frequency stored within the RCC registers of the chip.

	if(CPUFreq!=80000000) // If different from the frequency at which the NOMINAL state has to run.
	{
		vTaskSuspendAll(); // Suspend the Scheduler

		__disable_irq();        // Disable all interrupts: Peripherals, Systick...
		Periph_Disable_Clock(); // Disable the Peripherals Clocks
		SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // Stop the Systick timer

		Clock_80MHz(); // Clock configuration whose CPU frequency is 26 MHz

		CPUFreq = HAL_RCC_GetHCLKFreq(); // Update the SystemCoreClock
		SysTick_Config(SystemCoreClock / 1000); // Initialize the Systick and restart
		Periph_Enable_Clock();  // Enable the Peripherals Clocks
		__enable_irq();         // Enable all interrupts: Peripherals, Systick...
		HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

		xTaskResumeAll(); // Resume the Scheduler


		/*
		 * The function pdMS_TO_TICKS return ticks according to the CPU frequency.
		 * We need to update the timer period each time we change the state.
		 */

		xTimerChangePeriod(xTimerObc,pdMS_TO_TICKS(OBC_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerAdcs,pdMS_TO_TICKS(ADCS_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerEps,pdMS_TO_TICKS(EPS_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerPayload,pdMS_TO_TICKS(PAYLOAD_ACTIVE_PERIOD),0);
		// xTimerChangePeriod(xTimerRF,pdMS_TO_TICKS(PAYLOAD_ACTIVE_PERIOD),0);
	}

	Read_Flash(ANTENNA_DEPLOYED_ADDR, &deploy, 1);

	/* If we didnt have enough energy to deploy in INIT state. (INIT->CONTINGENCY)
	 * We do it once we enter NOMINAL for the first time.
	 */

	if(!deploy)
	{
		xTaskNotify(COMMS_Handle, ANTENNA_DEPLOYMENT_NOTI, eSetBits); // Deploy COMMS antenna.
		  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  // OBC TASK -> COMMS TASK.

		//xTaskNotify(RFI_Handle, ANTENNA_DEPLOYMENT_NOTI, eSetBits); // Deploy RF antenna.
		   	   	   	   	   	   	   	   	   	   	   	   	   	   	   	  // OBC TASK -> RFI TASK.

		deploy = 1;
		Send_to_WFQueue(&deploy, 1, ANTENNA_DEPLOYED_ADDR, OBCsender); // Save to the memory that we have already deployed the antennas.
	}


	EventBits_t EventBits;
	bool telecommand_process = false;

	/*
	 * telecommand_process is a boolean that determines whether or not a telecommand
	 * is on its way to be processed. If its the case, the software will not check
	 * if it has something to do (notifications or packets) because we already know.
	 * Then, the 15 s will be given directly.
	 */


	for(;;)
	{
		/*****COMMS PROCESS*****/

		if(!telecommand_process)
		{
			/*
			 * Give 500ms to COMMS TASK to check if :
			 * 		1. It has received a packet.
			 * 		2. It has received a notification.
			 */

			EventBits = xEventGroupWaitBits(xEventGroup, COMMS_RXIRQFlag_EVENT | COMMS_RXNOTI_EVENT, 0, false, pdMS_TO_TICKS(500));
		}

		if( ((EventBits & COMMS_RXIRQFlag_EVENT)==COMMS_RXIRQFlag_EVENT) || ((EventBits & COMMS_RXNOTI_EVENT)==COMMS_RXNOTI_EVENT))
		{
			/*
			 * If COMMS has something to do (packet or noti received)
			 * then give 15s additional seconds for COMMS for processing
			 * everything.
			 */

			EventBits = xEventGroupWaitBits(xEventGroup, COMMS_TELECOMMAND_EVENT | COMMS_WRONGPACKET_EVENT, 0, false, pdMS_TO_TICKS(15000));

			if((EventBits & COMMS_TELECOMMAND_EVENT)==COMMS_TELECOMMAND_EVENT)
			{
				/*
				 * Once the telecommand has been processed succesfully we proceed to
				 * check if COMMS TASK has sent any notification to OBC TASK.
				 */

				OBC_COMMS_RXFlags();
				xEventGroupClearBits(xEventGroup, COMMS_RXIRQFlag_EVENT | COMMS_TELECOMMAND_EVENT | COMMS_WRONGPACKET_EVENT); // Clear risen flags.
				telecommand_process = false; // Telecommand process finished.
			}

			else if (((EventBits & COMMS_TELECOMMAND_EVENT)!=COMMS_TELECOMMAND_EVENT) && ((EventBits & COMMS_WRONGPACKET_EVENT)!=COMMS_WRONGPACKET_EVENT))
			{
				/*
				 * If the telecommand has not been neither processed nor discarded
				 * it is assumed that the telecommand processing is still ongoing.
				 */

				telecommand_process = true; // Telecommand process ongoing.
			}
		}



		xTaskNotify(ADCS_Handle, CHECKROTATE_NOTI, eSetBits); // Poll lateral temperature sensors and determine if rotation is needed.
		  	  	  	  	  	  	  	  	  	  	  	  	  	  // OBC TASK -> ADCS TASK.

		xTaskNotify(EPS_Handle, EPS_HEATER_NOTI, eSetBits); // Poll temperature sensor of the battery and determine if heater activation is needed.
	  	  	  	  	  	  	  	  	  	  	  	  	  	  	// OBC TASK -> EPS TASK.

		xTaskNotify(EPS_Handle, EPS_BATTERY_NOTI, eSetBits); // Read battery.
 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 // EPS task updates the current state according to the battery capacity.
 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 // OBC TASK -> EPS TASK.
		// int8_t* tempCurrentState = currentState;
		Read_Flash(CURRENT_STATE_ADDR, (uint8_t *)&currentState, 1); // Read from the flash which is the current state.
		if(*currentState!=_NOMINAL){break;}

	}
}

void OBC_Contingency()
{
	uint32_t CPUFreq;
	uint8_t currentState, previousState;

	CPUFreq = HAL_RCC_GetHCLKFreq();
	if(CPUFreq != 26000000)
	{
		vTaskSuspendAll();

		__disable_irq();
		Periph_Disable_Clock();
		SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

		Clock_26MHz(); // Clock configuration whose CPU frequency is 26 MHz

		CPUFreq = HAL_RCC_GetHCLKFreq();
		SysTick_Config(SystemCoreClock / 1000);
		Periph_Enable_Clock();
		__enable_irq();

		xTaskResumeAll();

		//vTaskResume(RFI_Handle);

		xTimerChangePeriod(xTimerObc,pdMS_TO_TICKS(OBC_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerAdcs,pdMS_TO_TICKS(ADCS_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerEps,pdMS_TO_TICKS(EPS_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerPayload,pdMS_TO_TICKS(PAYLOAD_ACTIVE_PERIOD),0);
		// xTimerChangePeriod(xTimerRF,pdMS_TO_TICKS(PAYLOAD_ACTIVE_PERIOD),0);
	}


	Read_Flash(PREVIOUS_STATE_ADDR,&previousState,1);

	/*
	 * If we have transitioned from sunsafe to contingency it means that
	 * both ADCS and PAYLOAD tasks have been suspended, so we must resume
	 * them in this case.
	 */

	 if (previousState ==_SUNSAFE)
	 {
		 vTaskResume(ADCS_Handle   ); // Resume ADCS task
		 vTaskResume(PAYLOAD_Handle); // Resume PAYLOAD task
	 }

	xTaskNotify(COMMS_Handle, CONTINGENCY_NOTI, eSetBits); // Notify COMMS that we have entered the CONTINGENCY state
	xTaskNotify(ADCS_Handle,  CONTINGENCY_NOTI, eSetBits); // Notify ADCS that we have entered the CONTINGENCY state

	EventBits_t EventBits;
	bool telecommand_process = false;

	for(;;)
	{

		if(!telecommand_process)
		{
			EventBits = xEventGroupWaitBits(xEventGroup, COMMS_RXIRQFlag_EVENT | COMMS_RXNOTI_EVENT, 0, false, pdMS_TO_TICKS(500));
		}

		if( ((EventBits & COMMS_RXIRQFlag_EVENT)==COMMS_RXIRQFlag_EVENT) || ((EventBits & COMMS_RXNOTI_EVENT)==COMMS_RXNOTI_EVENT))
		{

			EventBits = xEventGroupWaitBits(xEventGroup, COMMS_TELECOMMAND_EVENT | COMMS_WRONGPACKET_EVENT, 0, false, pdMS_TO_TICKS(15000));

			if((EventBits & COMMS_TELECOMMAND_EVENT)==COMMS_TELECOMMAND_EVENT)
			{
				OBC_COMMS_RXFlags();
				xEventGroupClearBits(xEventGroup, COMMS_RXIRQFlag_EVENT | COMMS_TELECOMMAND_EVENT | COMMS_WRONGPACKET_EVENT);
				telecommand_process = false;
			}

			else if (((EventBits & COMMS_TELECOMMAND_EVENT)!=COMMS_TELECOMMAND_EVENT) && ((EventBits & COMMS_WRONGPACKET_EVENT)!=COMMS_WRONGPACKET_EVENT))
			{
				telecommand_process = true;
			}
		}

		xTaskNotify(ADCS_Handle, CHECKROTATE_NOTI, eSetBits);
		xTaskNotify(EPS_Handle, EPS_HEATER_NOTI, eSetBits);
		xTaskNotify(EPS_Handle, EPS_BATTERY_NOTI, eSetBits);

		Read_Flash(CURRENT_STATE_ADDR, &currentState, 1);
		if(currentState!=_CONTINGENCY){break;}
	}
}

void OBC_Sunsafe()
{
	uint32_t CPUFreq;
	uint8_t currentState, previousState;

	CPUFreq = HAL_RCC_GetHCLKFreq();
	if(CPUFreq != 8000000)
	{
		vTaskSuspendAll();

		__disable_irq();
		Periph_Disable_Clock();
		SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

		Clock_8MHz(); // Clock configuration whose CPU frequency is 8 MHz

		CPUFreq = HAL_RCC_GetHCLKFreq();
		SysTick_Config(SystemCoreClock / 1000);
		Periph_Enable_Clock();
		__enable_irq();

		xTaskResumeAll();


		xTimerChangePeriod(xTimerObc,pdMS_TO_TICKS(OBC_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerEps,pdMS_TO_TICKS(EPS_ACTIVE_PERIOD),0);
	}


	Read_Flash(PREVIOUS_STATE_ADDR,&previousState,1);

	xTaskNotify(COMMS_Handle, SUNSAFE_NOTI, eSetBits); // Notify COMMS that we have entered the SUNSAFE state.
	xTaskNotify(ADCS_Handle, SUNSAFE_NOTI, eSetBits);  // Notify ADCS that we have entered the SUNSAFE state.

	/*
	 * If we have transitioned from CONTINGENCY to SUNSAFE,
	 * both ADCS and PAYLOAD have to be suspended.
	 */

	if(previousState== _CONTINGENCY)
	{
		vTaskSuspend(ADCS_Handle);    // Suspend the ADCS task
		vTaskSuspend(PAYLOAD_Handle); // Suspend the PAYLOAD task
	}

	EventBits_t EventBits;
	bool telecommand_process = false;

	/****TIMER 7 CONFIGURATION FOR A 10s SLEEP*****/
	TIM7->SR&=~(1<<0); // Clear update interrupt flag
	TIM7->PSC = 40000; // 16-bit Prescaler : TIM7 frequency = CPU_F/PSC = 8.000.000/40.000 = 200 Hz.
	TIM7->ARR = 2000;  // 16-bit Auto Reload Register : ARR = Sleep_time * TIM7_F = 10*200=2000.

	for(;;)
	{
		TIM7->SR&=~(1<<0); // Clear update interrupt flag
		HAL_TIM_Base_Start_IT(&htim7);

		SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk; // Suspend the scheduler (systick timer) interrupt to not wake up the MCU
		HAL_SuspendTick();                          // Suspend the tim6 interrupts to not wake up the MCU


		HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI); // Enter in SLEEP MODE with the WFI (Wait For Interrupt) instruction

		/* SLEEPING ~ 10s */

		HAL_TIM_Base_Stop_IT(&htim7);

		SysTick->CTRL  |= SysTick_CTRL_TICKINT_Msk; // Enable the scheduler (systick timer) interrupt

		HAL_ResumeTick();                           // Enable the tim7 interrupt


		if(!telecommand_process)
		{
			EventBits = xEventGroupWaitBits(xEventGroup, COMMS_RXIRQFlag_EVENT | COMMS_RXNOTI_EVENT, 0, false, pdMS_TO_TICKS(500));
		}

		if( ((EventBits & COMMS_RXIRQFlag_EVENT)==COMMS_RXIRQFlag_EVENT) || ((EventBits & COMMS_RXNOTI_EVENT)==COMMS_RXNOTI_EVENT))
		{

			EventBits = xEventGroupWaitBits(xEventGroup, COMMS_TELECOMMAND_EVENT | COMMS_WRONGPACKET_EVENT, 0, false, pdMS_TO_TICKS(15000));

			if((EventBits & COMMS_TELECOMMAND_EVENT)==COMMS_TELECOMMAND_EVENT)
			{
				OBC_COMMS_RXFlags();
				xEventGroupClearBits(xEventGroup, COMMS_RXIRQFlag_EVENT | COMMS_TELECOMMAND_EVENT | COMMS_WRONGPACKET_EVENT);
				telecommand_process = false;
			}

			else if (((EventBits & COMMS_TELECOMMAND_EVENT)!=COMMS_TELECOMMAND_EVENT) && ((EventBits & COMMS_WRONGPACKET_EVENT)!=COMMS_WRONGPACKET_EVENT))
			{
				telecommand_process = true;
			}
		}

		xTaskNotify(EPS_Handle, EPS_HEATER_NOTI, eSetBits);
		xTaskNotify(EPS_Handle, EPS_BATTERY_NOTI, eSetBits);

		Read_Flash(CURRENT_STATE_ADDR, &currentState, 1);
		if(currentState!=_SUNSAFE){break;}
	}
}

void OBC_Survival()
{
	uint32_t CPUFreq;
	uint8_t currentState, previousState;

	CPUFreq = HAL_RCC_GetHCLKFreq();

	if(CPUFreq != 2000000)
	{
		vTaskSuspendAll();

		__disable_irq();
		Periph_Disable_Clock();
		SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

		Clock_2MHz(); // Clock configuration whose CPU frequency is 2 MHz

		CPUFreq = HAL_RCC_GetHCLKFreq();
		SysTick_Config(SystemCoreClock / 1000);
		Periph_Enable_Clock();
		__enable_irq();

		xTaskResumeAll();

		xTimerChangePeriod(xTimerObc,pdMS_TO_TICKS(OBC_ACTIVE_PERIOD),0);
		xTimerChangePeriod(xTimerEps,pdMS_TO_TICKS(EPS_ACTIVE_PERIOD),0);
	}

	Read_Flash(PREVIOUS_STATE_ADDR, &previousState, 1);
	if(previousState==_SUNSAFE)
	{
		vTaskSuspend(COMMS_Handle);
	}

	HAL_PWREx_EnableLowPowerRunMode(); // Enter into LOW POWER RUN MODE
	/****TIMER 7 CONFIGURATION FOR A 10s SLEEP*****/
	TIM7->SR&=~(1<<0); // Clear update interrupt flag
	TIM7->PSC = 20000; // 16-bit Prescaler : TIM2 frequency = CPU_F/PSC = 2.000.000/20.000 = 100 Hz.
	TIM7->ARR = 1000;  // 16-bit Auto Reload Register : ARR = Sleep_time * TIM7_F = 10*100=1000.

	for(;;)
	{
		TIM7->SR&=~(1<<0);
		HAL_TIM_Base_Start_IT(&htim7);

		SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
		HAL_SuspendTick();

		HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);

		/* SLEEPING ~ 10s */

		HAL_TIM_Base_Stop_IT(&htim7);
		SysTick->CTRL  |= SysTick_CTRL_TICKINT_Msk;
		HAL_ResumeTick();

		xTaskNotify(EPS_Handle, EPS_HEATER_NOTI, eSetBits);
		xTaskNotify(EPS_Handle, EPS_BATTERY_NOTI, eSetBits);

		Read_Flash(CURRENT_STATE_ADDR, &currentState, 1);
		if(currentState!=_SURVIVAL){break;}
	}

	/*
	 * When exiting the SURVIVAL state we need to make sure,
	 * we are exiting the Low Power Run mode as well.
	 */

	HAL_PWREx_DisableLowPowerRunMode();
}




