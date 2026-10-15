/***********************************************************************************************************************
 * Copyright [2020-2021] Renesas Electronics Corporation and/or its affiliates.  All Rights Reserved.
 *
 * This software and documentation are supplied by Renesas Electronics America Inc. and may only be used with products
 * of Renesas Electronics Corp. and its affiliates ("Renesas").  No other uses are authorized.  Renesas products are
 * sold pursuant to Renesas terms and conditions of sale.  Purchasers are solely responsible for the selection and use
 * of Renesas products and Renesas assumes no liability.  No license, express or implied, to any intellectual property
 * right is granted by Renesas. This software is protected under all applicable laws, including copyright laws. Renesas
 * reserves the right to change or discontinue this software and/or this documentation. THE SOFTWARE AND DOCUMENTATION
 * IS DELIVERED TO YOU "AS IS," AND RENESAS MAKES NO REPRESENTATIONS OR WARRANTIES, AND TO THE FULLEST EXTENT
 * PERMISSIBLE UNDER APPLICABLE LAW, DISCLAIMS ALL WARRANTIES, WHETHER EXPLICITLY OR IMPLICITLY, INCLUDING WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT, WITH RESPECT TO THE SOFTWARE OR
 * DOCUMENTATION.  RENESAS SHALL HAVE NO LIABILITY ARISING OUT OF ANY SECURITY VULNERABILITY OR BREACH.  TO THE MAXIMUM
 * EXTENT PERMITTED BY LAW, IN NO EVENT WILL RENESAS BE LIABLE TO YOU IN CONNECTION WITH THE SOFTWARE OR DOCUMENTATION
 * (OR ANY PERSON OR ENTITY CLAIMING RIGHTS DERIVED FROM YOU) FOR ANY LOSS, DAMAGES, OR CLAIMS WHATSOEVER, INCLUDING,
 * WITHOUT LIMITATION, ANY DIRECT, CONSEQUENTIAL, SPECIAL, INDIRECT, PUNITIVE, OR INCIDENTAL DAMAGES; ANY LOST PROFITS,
 * OTHER ECONOMIC DAMAGE, PROPERTY DAMAGE, OR PERSONAL INJURY; AND EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH LOSS, DAMAGES, CLAIMS OR COSTS.
 **********************************************************************************************************************/
#include "dialog_wifi_demo.h"
#include "da16200_AT.h"
#include "hal_data.h"
//#include "http_server.h"
//#include "common_utils.h"

demo_data_t g_demo_data;
//static uint8_t http_ip_port[500];
//static void dialog_wifi_demo_init(void);
/* here is my local HTML contents */


uint8_t user_buffer[2048];

static char AT_S[] = "S1150,183.230.40.33,80,POST /devices/1033070521/datapoints?type=3 HTTP/1.1 \r\n"
"api-key:bFhv70DeSHSnxmqOavDxGxgSu6U=\r\n"
"Host:api.heclouds.com\r\n"
"Content-Length: 12\r\n\r\n";
static char TCP_DATA[] = "{'temp':";

static char TCP_ERR[] = "ERROR:";

/************************************************************************************
* Name:       wifi_TCP_SEND_DATA
* Function:   Operate  command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_TCP_SEND_DATA(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_512] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    uint8_t expected_resp_present;

    do
    {
        bytes_write = (uint16_t) strlen((char *) user_buffer);
        wifi_serial_write(user_buffer,bytes_write);

        bytes_read = 512;
        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 2000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char *)TCP_ERR);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                //retval = FSP_SUCCESS;
                //break;
            }
            else
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(1000,BSP_DELAY_UNITS_MILLISECONDS);
        ++retry_count;

    }
    while(retry_count < 5);

    return retval;
    //return FSP_SUCCESS;
}


/************************************************************************************
* Name:       dialog_wifi_demo
* Function:   run dialog wifi demo
* Parameters: none
* Return:     none
************************************************************************************/
void dialog_wifi_demo(void)
{

//    http_receive_status http_status;
    static unsigned char operation = 0;
    static uint8_t cnt=0;
    fsp_err_t err;

   while(1)
   {
       switch(operation)
       {
           case 0:
                //Wi-Fi initializatoin
                g_ioport.p_api->pinWrite(g_ioport.p_ctrl, LED1, BSP_IO_LEVEL_HIGH);
                wifi_init();
                //Wi-Fi setting
                err = wifi_set();
                g_ioport.p_api->pinWrite(g_ioport.p_ctrl, LED1, BSP_IO_LEVEL_LOW);
                if(err)
                {
                    R_BSP_SoftwareDelay(2000,BSP_DELAY_UNITS_MILLISECONDS);
                }
                else
                {
                    R_BSP_SoftwareDelay(2000,BSP_DELAY_UNITS_MILLISECONDS);
                    operation = 1;
                }
                break;

           case 1:
               cnt++;
               //memset (user_buffer, " " , sizeof(user_buffer));
               sprintf((char *)user_buffer,"%s%s%3d%s",(char *)AT_S,(char *)TCP_DATA,(uint8_t)cnt,"}\r\n");
               err = wifi_TCP_SEND_DATA();
               if(err)
               {
                   cnt--;
                   R_BSP_SoftwareDelay(2000,BSP_DELAY_UNITS_MILLISECONDS);
                   operation = 0;
               }
               else
               {
                   R_BSP_SoftwareDelay(1000,BSP_DELAY_UNITS_MILLISECONDS);
                   g_ioport.p_api->pinWrite(g_ioport.p_ctrl, LED2, BSP_IO_LEVEL_HIGH);
                   R_BSP_SoftwareDelay(1000,BSP_DELAY_UNITS_MILLISECONDS);
                   g_ioport.p_api->pinWrite(g_ioport.p_ctrl, LED2, BSP_IO_LEVEL_LOW);
               }
               break;
           default:
               operation = 0;
               break;
       }
   }
}




