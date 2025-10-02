#ifndef INC_OBC_H_
#define INC_OBC_H_

// TODO: revisar stack sizes y prioridades!

// Task stack sizes
#define OBC_STACK_SIZE			1000 // ??
#define PAYLOAD_STACK_SIZE		4000 // ??
#define EPS_STACK_SIZE		    250 // ??
#define OBDH_STACK_SIZE        1000 // ??

// Task priorities
#define OBC_PRIORITY             7 // ??
#define PAYLOAD_PRIORITY         2 // ?? Mirar!!
#define EPS_PRIORITY             2 // ?? 
#define OBDH_PRIORITY            5 // ??

/**
 * @brief Communications task function, it runs the OBC state machine.
 */
void obc_task(void *pv_parameters);

#endif /* INC_OBC_H_ */