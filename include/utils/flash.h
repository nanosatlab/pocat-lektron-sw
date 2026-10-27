/**
 * @file flash.h
 * @brief Internal flash driver.
 * @details
 * Programs, erases and reads internal flash inside the regions of flash_map.h.
 * Each call names a region and an offset inside it, and the driver refuses any
 * range outside that region (spec FLS-DRV-010). For task-safe access at runtime
 * use the OBDH-mediated request functions in obdh_requests.h instead.
 *
 * The old address map below, with flash_write() and flash_read_old(), stays
 * until the parameter store replaces it (step 4 of docs/FLASH-USAGE-SPECIFICATION.md §10).
 * @author Medir Segura
 * @date 2023-01-17
 * @note Modified on 2026-10-27.
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

#include "stm32l4xx_hal.h"

#include "obc.h"
#include "definitions.h"
#include "flash_map.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"

// Old memory map, replaced by flash_map.h. Do not add addresses here: these are
// deleted once the parameter store has taken over their contents.

// The three pages of bank 1 that the old addresses use (0x08030000-0x080317FF).
// flash_write() and flash_read_old() refuse anything outside them.
#define OLD_MAP_BASE				0x08030000u
#define OLD_MAP_SIZE				0x00001800u

#define PHOTO_ADDR 					0x08040000
#define COMMS_CONFIG_ADDR
#define COMMS_CONFIG_ADDR
#define COMMS_CONFIG_ADDR

//#define PAYLOAD_STATE_ADDR 		0x08008000
//#define COMMS_STATE_ADDR 			0x08008001
//#define DETUMBLE_STATE_ADDR 		0x08008004
#define CURRENT_STATE_ADDR			0x08030000 //OBC STATE MACHINE CURRENT STATE
#define PREVIOUS_STATE_ADDR			0x08030001 //OBC STATE MACHINE PREVIOUS STATE
#define DEPLOYMENT_STATE_ADDR 		0x08030002 //DEPLOYMENT STATE
#define DEPLOYMENTRF_STATE_ADDR 	0x08030003 //ANTENNA DEPLYMENT STATE

#define EXIT_LOW_ADDR 				0x08030007
#define SET_TIME_ADDR				0x08030008 	//4 bytes (GS -> RTC)
#define RTC_TIME_ADDR				0x0803000C	//4 bytes (RTC -> Unix) Get from the RTC

//CONFIGURATION ADDRESSES
#define CONFIG_ADDR 				0x08030010
#define KP_ADDR 					0x08030010
#define GYRO_RES_ADDR 				0x08030011
#define SF_ADDR 					0x08030012
#define CRC_ADDR 					0x08030013
#define PHOTO_RESOL_ADDR 			0x08030014
#define PHOTO_COMPRESSION_ADDR 		0x08030015
#define F_MIN_ADDR 					0x08030016
#define F_MAX_ADDR 					0x08030018
#define DELTA_F_ADDR 				0x0803001A
#define INTEGRATION_TIME_ADDR 		0x0803001C

#define TLE_ADDR 					0x08030020 // 138 bytes
//#define EXIT_LOW_POWER_FLAG_ADDR 	0x080080AA

//CALIBRATION ADDRESSES
#define CALIBRATION_ADDR			0x080300AB
#define MAGNETO_MATRIX_ADDR			0x080300AB // 35 bytes
#define MAGNETO_OFFSET_ADDR			0x080300CF // 11 bytes
#define GYRO_POLYN_ADDR 			0x080300DB // 23 bytes
#define PHOTODIODES_OFFSET_ADDR 	0x080300F3 // 12 bytes

//TELEMETRY ADDRESSES
#define TELEMETRY_ADDR				0x08030100
#define TEMPLAT_ADDR 				0x08030100		// 6
#define BATT_TEMP_ADDR 			    0x08030106		// 1
#define MCU_TEMP_ADDR 			    0x08030107		// 1
#define BATT_CAP_ADDR 			    0x08030108		// 3
//      CURRENT_STATE_ADDR                             1
#define GYRO_ADDR 			        0x08030109		// 6
#define MAGNETOMETER_ADDR 			0x0803010F      // 8
#define PHOTODIODES_ADDR 			0x08030117      // 8

//TIME ADDR
#define PL_TIME_ADDR 				0x08030111 	//4 bytes (GS -> PAYLOAD CAMERA)
#define PL_RF_TIME_ADDR				0x0803011C	//8 bytes (GS -> PAYLOAD RF)

//COMMS CONFIGURATION ADDRESSES
#define COUNT_PACKET_ADDR 			0x08030200
#define COUNT_WINDOW_ADDR 			0x08030201
#define COUNT_RTX_ADDR 				0x08030202

#define TIMEOUT_ADDR 				0x08030204 // 2 bytes

#define ANTENNA_DEPLOYED_ADDR       0x08030205

#define BOOT_TIME_ADDR              0x08030206 // 4 bytes (OBC boot unix time; beacon uptime = now - boot)


/**********OTHER ADDR****************************/
#define DATA_ADDR					0x08030000
#define TLE_ADDR1 					0x08038020 // 138 bytes
#define TLE_ADDR2 					0x08038065 // 138 bytes
#define COMMS_TIME_ADDR				0x0803E860 // Time between packets
#define PHOTOTIME_ADDR				0x08038008 	//4 bytes (GS -> RTC)
#define COMMS_STATE_ADDR			0x08038012
#define COMMS_BOOL_ADDR				0x08038013
/************************************************/



//EPS
#define EPS_THRESHOLDS_ADDR			0x08030800 // 4 bytes (1 byte per threshold)
#define NOMINAL_TH_ADDR             0x08030800  // 1
#define CONTINGENCY_TH_ADDR         0x08030801  // 1
#define SUNSAFE_TH_ADDR             0x08030802  // 1
#define SURVIVAL_TH_ADDR            0x08030803  // 1

#define RFI_CONFIG_ADDR             0x08031000  // 8


extern EventGroupHandle_t xEventGroup;
extern QueueHandle_t FLASH_Queue;
extern SemaphoreHandle_t xMutex;

/* ---- Driver: regions of flash_map.h ---- */

/**
  * @brief  Reads bytes from a region into a RAM buffer.
  * @details Internal flash is memory-mapped, so the read is a plain copy.
  *          Access is serialized through the OBDH queue at runtime.
  * @param  region: Region of flash_map.h.
  * @param  offset: First byte to read, counted from the start of the region.
  * @param  data: Destination buffer (must hold at least n_bytes).
  * @param  n_bytes: Number of bytes to read.
  * @retval HAL_OK; HAL_ERROR if the range is not inside the region (nothing is copied).
  */
HAL_StatusTypeDef flash_read(flash_region_id_t region, uint32_t offset, uint8_t *data, uint32_t n_bytes);

/**
  * @brief  Programs a byte buffer into already-erased flash of a region, without erasing.
  * @details Writes doublewords directly with HAL_FLASH_Program. The destination
  *          must read as erased (all 0xFF).
  * @param  region: Region of flash_map.h. Unit data, written on the ground, is refused.
  * @param  offset: Destination, counted from the start of the region (must be a multiple of 8).
  * @param  data: Pointer to the source buffer.
  * @param  n_bytes: Number of bytes to program (must be a multiple of 8).
  * @retval HAL_OK on success; HAL_ERROR if the range is not inside the region, on
  *         bad alignment, non-erased target or HAL programming failure.
  */
HAL_StatusTypeDef flash_program(flash_region_id_t region, uint32_t offset, const uint8_t *data, uint32_t n_bytes);

/**
  * @brief  Erases the 2 KB page of a region that contains the given offset.
  * @param  region: Region of flash_map.h. Unit data, written on the ground, is refused.
  * @param  offset: Any offset inside the page to erase, counted from the start of the region.
  * @retval HAL status of the erase operation; HAL_ERROR if the offset is not inside the region.
  */
HAL_StatusTypeDef flash_erase_page(flash_region_id_t region, uint32_t offset);

/* ---- Old map: deleted with the old addresses in step 4 ---- */

/**
  * @brief  Writes a byte buffer to the old map pages, spanning multiple pages if needed.
  * @details Each flash page touched by the destination range is read into RAM,
  *          updated with the new bytes, erased, then programmed back one
  *          doubleword at a time, using a single page-sized RAM buffer.
  * @param  data_addr: Destination start address in flash.
  * @param  data: Pointer to the source buffer.
  * @param  n_bytes: Number of bytes to write.
  * @retval HAL_OK on success; HAL_ERROR if the range is not inside the old map
  *         pages (OLD_MAP_BASE, OLD_MAP_SIZE) or an erase or program step failed.
  * @todo   Consider FLASH_TYPEPROGRAM_FAST (256-byte row programming) in the future.
  * @todo   This was implemented to support multi-page writes; if that turns out
  *         not to be necessary, the function can be simplified back to a single page.
  * @todo   Review __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
  */
HAL_StatusTypeDef flash_write(uint32_t data_addr, const uint8_t *data, uint16_t n_bytes);

/**
  * @brief  Reads bytes from the old map pages into a RAM buffer.
  * @details Access is serialized through the OBDH queue at runtime; the one
  *          direct caller (main) runs before the scheduler starts.
  * @param  data_addr: Source start address in flash.
  * @param  data: Destination buffer (must hold at least n_bytes).
  * @param  n_bytes: Number of bytes to read.
  * @retval HAL_OK; HAL_ERROR if the range is not inside the old map pages
  *         (OLD_MAP_BASE, OLD_MAP_SIZE); nothing is copied.
  */
HAL_StatusTypeDef flash_read_old(uint32_t data_addr, uint8_t *data, uint32_t n_bytes);

#endif /* INC_FLASH_H_ */
