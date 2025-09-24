/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for non-secure
*                    application in the CM33 CPU
*
* Related Document : See README.md
*
********************************************************************************
* Copyright 2023-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cybsp.h"
#include "retarget_io_init.h"

#include "USB.h"
#include "USB_CDC.h"
#include <stdio.h>

/*******************************************************************************
* Macros
*******************************************************************************/
/* The timeout value in microsecond used to wait for core to be booted */
#define CM55_BOOT_WAIT_TIME_USEC       (10U)

/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR          (CYMEM_CM33_0_m55_nvm_START + \
                                            CYBSP_MCUBOOT_HEADER_SIZE)

/* Defines how often a message is printed in milliseconds */
#define MESSAGE_PRINT_PERIOD           (3000u)

/* USBD delay function*/
#define USB_RESUME_DELAY_MS            (10U)
#define USB_SUSPEND_DELAY_MS           (1000U)

/* Timeout for USBD write */
#define USB_WRITE_TIMEOUT_MS           (100U)
#define USB_INT_INTERVAL               (64U)
#define SYSTEM_DELAY_MS                (250U)
#define APP_INTR_PRIORITY              (3U)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
/*USBD OS timer fuctions definitions in middleware library*/
void usbd_timer_config(void);
void usbd_timer_config_deinit(void);

static const USB_DEVICE_INFO usb_device_info =
{
    0x058A,                   /* VendorId */
    0x027A,                   /* ProductId */
    "Infineon Technologies",  /* VendorName */
    "CDC Code Example",       /* ProductName */
    "12345678"                /* SerialNumber */
};

static USB_CDC_HANDLE h_inst;

static U8 usb_out_buffer[USB_HS_BULK_MAX_PACKET_SIZE];

static volatile uint16_t usb_msg_counter = 0u;
static volatile bool usb_suspend_flag    = false;
static volatile bool usb_resume_flag     = false;

/* Timer interrupt configuration for Suspend resume detection over USB  */
const cy_stc_sysint_t suspend_resume_detection_irq_cfg =
{
    .intrSrc      = SUSPEND_RESUEME_DETECTION_TIMER_IRQ,
    .intrPriority = APP_INTR_PRIORITY
};

/*******************************************************************************
* Function Name: resume_usb_device
********************************************************************************
* Summary:
*  Start the emUSB_OS_Timer, suspend_resume_detection_timer and turn ON the LED
*  when resume condition is detected over USB.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void resume_usb_device(void)
{
    /* Turn ON the USER LED1 to indicate that USB is in active mode */
    Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, CYBSP_LED_STATE_ON);

    /* Start suspend_resume_detection_timer */
    Cy_TCPWM_Counter_Enable(SUSPEND_RESUEME_DETECTION_TIMER_HW,
            SUSPEND_RESUEME_DETECTION_TIMER_NUM);
    Cy_TCPWM_TriggerStart_Single(SUSPEND_RESUEME_DETECTION_TIMER_HW,
            SUSPEND_RESUEME_DETECTION_TIMER_NUM);

    /* Start emUSB_OS_Timer */
    usbd_timer_config();
}

/*******************************************************************************
* Function Name: suspend_resume_detection_irq_handler
********************************************************************************
* Summary:
*  One millisecond timer interrupt handler to detect suspend and resume events
*  over USB.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void suspend_resume_detection_irq_handler(void)
{
    uint32_t interrupts = Cy_TCPWM_GetInterruptStatusMasked(
            SUSPEND_RESUEME_DETECTION_TIMER_HW,
            SUSPEND_RESUEME_DETECTION_TIMER_NUM);

    /* Clear the interrupt */
    Cy_TCPWM_ClearInterrupt(SUSPEND_RESUEME_DETECTION_TIMER_HW,
            SUSPEND_RESUEME_DETECTION_TIMER_NUM, interrupts);

    if (USB_STAT_CONFIGURED != (USBD_GetState() & (USB_STAT_CONFIGURED |
            USB_STAT_SUSPENDED)))
    {
        /* Suspend condition on USB bus is detected.
         * Request device to enter low-power mode
         */
        usb_suspend_flag = true;
    }
    else
    {
        /* Clear suspend conditions */
        usb_resume_flag = true;
        usb_suspend_flag = false;

        /* Counter to print USB message */
        usb_msg_counter++;
    }
}

/*******************************************************************************
* Function Name: suspend_resume_detection_timer
********************************************************************************
* Summary:
*  Initializes suspend resume detection timer
*
* Parameters:
*  None
*
* Return:
*  void
*
*******************************************************************************/
static void suspend_resume_detection_timer(void)
{
    /* Initialize TCPWM to implement suspend resume detection timer */
    if (CY_TCPWM_SUCCESS != Cy_TCPWM_Counter_Init
            (SUSPEND_RESUEME_DETECTION_TIMER_HW,
                    SUSPEND_RESUEME_DETECTION_TIMER_NUM,
                    &SUSPEND_RESUEME_DETECTION_TIMER_config))
    {
        handle_app_error();
    }

    /* Enable suspend resume detection timer */
    Cy_TCPWM_Counter_Enable(SUSPEND_RESUEME_DETECTION_TIMER_HW,
            SUSPEND_RESUEME_DETECTION_TIMER_NUM);

    /* Set the interrupt line for SUSPEND_RESUEME_DETECTION_TIMER_HW */
    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&suspend_resume_detection_irq_cfg,
            suspend_resume_detection_irq_handler))
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(SUSPEND_RESUEME_DETECTION_TIMER_IRQ);

    /* Start suspend resume detection timer */
    Cy_TCPWM_TriggerStart_Single(SUSPEND_RESUEME_DETECTION_TIMER_HW,
            SUSPEND_RESUEME_DETECTION_TIMER_NUM);
}

/*******************************************************************************
* Function Name: usb_add_cdc
********************************************************************************
* Summary:
*  Add CDC device to the emUSB-Device middleware.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void usb_add_cdc(void)
{
    USB_CDC_INIT_DATA initData;
    USB_ADD_EP_INFO   EPBulkIn;
    USB_ADD_EP_INFO   EPBulkOut;
    USB_ADD_EP_INFO   EPIntIn;

    memset(&initData, 0, sizeof(initData));
    memset(&EPBulkIn, 0, sizeof(EPBulkIn));
    memset(&EPBulkOut, 0, sizeof(EPBulkOut));
    memset(&EPIntIn, 0, sizeof(EPIntIn));

    /* Bulk In endpoint descriptor */
    EPBulkIn.Flags          = 0u;           /* Flags not used. */
    EPBulkIn.InDir          = USB_DIR_IN;   /* IN direction (Device to Host) */
    EPBulkIn.Interval       = 0u;  /* Interval not used for Bulk endpoints. */

    /* Maximum packet size (512 for Bulk in high-speed). */
    EPBulkIn.MaxPacketSize  = USB_HS_BULK_MAX_PACKET_SIZE;

    EPBulkIn.TransferType   = USB_TRANSFER_TYPE_BULK;/* Endpoint type - Bulk.*/

    /* Add Bulk In endpoint */
    initData.EPIn           = USBD_AddEPEx(&EPBulkIn, NULL, 0);

    /* Bulk Out endpoint descriptor */
    EPBulkOut.Flags         = 0u;          /* Flags not used. */
    EPBulkOut.InDir         = USB_DIR_OUT; /* OUT direction (Host to Device) */
    EPBulkOut.Interval      = 0u;   /* Interval not used for Bulk endpoints. */

    /* Maximum packet size (512 for Bulk out high-speed). */
    EPBulkOut.MaxPacketSize = USB_HS_BULK_MAX_PACKET_SIZE;

    EPBulkOut.TransferType  = USB_TRANSFER_TYPE_BULK;/* Endpoint type - Bulk.*/

    /* Add Bulk Out endpoint */
    initData.EPOut          = USBD_AddEPEx(&EPBulkOut, usb_out_buffer,
                                           sizeof(usb_out_buffer));

    /* Interrupt In endpoint descriptor */
    EPIntIn.Flags           = 0u;           /* Flags not used. */
    EPIntIn.InDir           = USB_DIR_IN;   /* IN direction (Device to Host) */

    EPIntIn.Interval        = USB_INT_INTERVAL;/* Interval of 1ms (125us * 8) */

    /* Maximum packet size (512 for Interrupt in high-speed). */
    EPIntIn.MaxPacketSize   = USB_HS_INT_MAX_PACKET_SIZE;

    /* Endpoint type - Interrupt. */
    EPIntIn.TransferType    = USB_TRANSFER_TYPE_INT;

    /* Add Interrupt In endpoint */
    initData.EPInt          = USBD_AddEPEx(&EPIntIn, NULL, 0);

    /* Adds a CDC device class to the USB middleware */
    h_inst = USBD_CDC_Add(&initData);
}

/*******************************************************************************
* Function Name: print_message
********************************************************************************
* Summary:
*  Print USB active message on the serial terminal connected over USB.
*
* Parameters:
*  msg: msg to be written on USB CDC port.
*
* Return:
*  void
*
*******************************************************************************/
 void print_message(const char msg[])
 {
    int msg_len = strlen(msg);
    if (msg_len < sizeof(usb_out_buffer))
    {
        memset(usb_out_buffer, 0, sizeof(usb_out_buffer));
        memcpy(usb_out_buffer, (U8*) msg, msg_len);
        USBD_CDC_Write(h_inst, &usb_out_buffer, sizeof(usb_out_buffer),
                       USB_WRITE_TIMEOUT_MS);
    }
 }

 /*******************************************************************************
 * Function Name: sleep_callback
 ********************************************************************************
 * Summary:
 *  SLEEP callback implementation. Sleep callback function is executed when the
 *  CM33 CPU goes to sleep mode
 *
 * Parameters:
 *  state - Power mode transition state of the CPU
 *  mode  - callback mode
 *  callback_arg - user argument (not used)
 *
 * Return:
 *  false - if transition is prohibited
 *  true  - if allowed
 *
 *******************************************************************************/
 static cy_en_syspm_status_t sleep_callback(cy_stc_syspm_callback_params_t *callbackParams, cy_en_syspm_callback_mode_t mode)
 {
     CY_UNUSED_PARAMETER(callbackParams);

     bool status = true;

     switch (mode)
     {
         case CY_SYSPM_CHECK_READY:
             if (usb_suspend_flag)
             {
                 /* Stop suspend_resume_detection_timer */
                 Cy_TCPWM_Counter_Disable(SUSPEND_RESUEME_DETECTION_TIMER_HW,
                         SUSPEND_RESUEME_DETECTION_TIMER_NUM);

                 /* Stop emUSB_OS_Timer */
                 usbd_timer_config_deinit();
             }
             else
             {
                 status = false;
             }
             break;

         case CY_SYSPM_CHECK_FAIL:
             if (usb_suspend_flag)
             {
                 /* Start the suspend_resume_detection_timer */
                 Cy_TCPWM_Counter_Enable(SUSPEND_RESUEME_DETECTION_TIMER_HW,
                         SUSPEND_RESUEME_DETECTION_TIMER_NUM);
                 Cy_TCPWM_TriggerStart_Single(SUSPEND_RESUEME_DETECTION_TIMER_HW,
                         SUSPEND_RESUEME_DETECTION_TIMER_NUM);

                 /* Start the emUSB_OS_Timer */
                 usbd_timer_config();
             }
             break;

         case CY_SYSPM_BEFORE_TRANSITION:
             if (usb_suspend_flag)
             {
                 Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN,
                         CYBSP_LED_STATE_OFF);
             }
             break;

         case CY_SYSPM_AFTER_TRANSITION:
             resume_usb_device();
             break;

         default:
             /* Should NOT happens */
             handle_app_error();
             break;
     }

     if (status)
     {
         return CY_SYSPM_SUCCESS;
     }
     else
     {
         return CY_SYSPM_FAIL;
     }
 }
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* Summary:
*  This is the main function for CM33 non-secure CPU. It initializes the
*  USB device block and numerates as a CDC device. When the USB suspend
*  condition is detected, it sends the device to a low power state, and
*  restores normal operation when USB activity resumes.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Syspm callback to handle CPU sleep for USB */
    cy_stc_syspm_callback_params_t syspmSleepAppParams;
      cy_stc_syspm_callback_t syspmAppSleepCallbackHandler =
    {sleep_callback, CY_SYSPM_SLEEP, 0u, &syspmSleepAppParams, NULL, NULL, 0};

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }
    
    /* Enable global interrupts */
    __enable_irq();
    
    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("****************** "
           "PSOC Edge MCU: emUSB-Device suspend and resume "
           "****************** \r\n\n");

    /* Power Management callback registration */
    if(! Cy_SysPm_RegisterCallback(&syspmAppSleepCallbackHandler)){
            printf("Failed to register syspmAppSleepCallbackHandler\r\n");}

    if (CY_RSLT_SUCCESS != result)
    {
        printf("Failed to register syspm callback\r\n");
    }

    /* Initializes the USB device with its settings */
    USBD_Init();

    /* Add the CDC device */
    usb_add_cdc();

    /* Set USB device info to be used during device enumeration */
     USBD_SetDeviceInfo(&usb_device_info);

    /* Starts the emUSB-Device Core */
    USBD_Start();

    /* Make device appear on the bus. This function call is blocking,
     * toggle the kit user LED until device gets enumerated.
     */
    while (USB_STAT_CONFIGURED != (USBD_GetState() & (USB_STAT_CONFIGURED |
             USB_STAT_SUSPENDED)))
    {
        Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
        Cy_SysLib_Delay(SYSTEM_DELAY_MS);
    }

    /* Initialization of applicaiton timer*/
    suspend_resume_detection_timer();

    /* Turn ON the kit user LED to indicate USB device is enumerated */
    Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, CYBSP_LED_STATE_ON);
    
    /* Enable CM55. */
    /* CM55_APP_BOOT_ADDR must be updated if CM55 memory layout is changed.*/
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);

    for (;;)
    {
        Cy_SysLib_Delay(SYSTEM_DELAY_MS);
        /* Check if suspend condition is detected on the bus */
        if (usb_suspend_flag)
        {
            /* Power management mode: Sleep */
            printf("Device is going to suspend\r\n");

            /* Delay for printout character buffer */
            USB_OS_Delay(USB_SUSPEND_DELAY_MS);

            /* Try to enter SLEEP mode */
            if (CY_RSLT_SUCCESS != Cy_SysPm_CpuEnterSleep(CY_SYSPM_WAIT_FOR_INTERRUPT))
            {
                printf("Entering SLEEP failed!\n\r");
            }
            else
            {
                if (usb_resume_flag)
                {
                    usb_resume_flag = false;
                    printf("Resume event from Host\n\r");

                    /* Resume recovery time */
                    USB_OS_Delay(USB_RESUME_DELAY_MS);
                }
            }
        }
        else if ((!usb_suspend_flag)&(USB_STAT_CONFIGURED == (USBD_GetState()
                 & (USB_STAT_CONFIGURED | USB_STAT_SUSPENDED))))
        {
            /* Check if a message should be printed to the console */
            if (MESSAGE_PRINT_PERIOD < usb_msg_counter)
            {
                /* Reset message counter */
                usb_msg_counter = 0;
                /* Print message to the console */
                print_message("USB is active\r\n");
            }
        }
    }
}

/* END OF FILE [] */
