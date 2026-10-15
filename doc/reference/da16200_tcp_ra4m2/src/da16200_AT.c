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
#include "hal_data.h"
#include "da16200_AT.h"
#include "dialog_wifi_demo.h"


/** AT Command sets */
/*LDRA_INSPECTED 27 D This structure must be accessible in user code. It cannot be static. */
da16200_at_cmd_set_t g_da16200_cmd_set[] =
{
    /** Intial AT command */
    [DA16200_AT_CMD_INDEX_ATZ] =
    {
        .p_cmd = (uint8_t *) "ATZ\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_64,
        .retry = DA16200_RETRY_VALUE_10,
        .retry_delay = DA16200_DELAY_500MS
    },
    /** Echo on/ off */
    [DA16200_AT_CMD_INDEX_ATE] =
    {
        .p_cmd = (uint8_t *) "ATE\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_32,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* ATF */
    [ DA16200_AT_CMD_INDEX_AT_ATF] =
    {
        .p_cmd = (uint8_t *) "ATF\r\n",
        .p_success_resp = (uint8_t *) "DONE",
        .max_resp_length = DA16200_STR_LEN_128,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_500MS
    },
    /* Set boot mode */
    [DA16200_AT_CMD_INDEX_AT_TMRFNOINIT] =
    {
        .p_cmd = (uint8_t *) "AT+TMRFNOINIT=0\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_32,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* Set Station mode */
    [DA16200_AT_CMD_INDEX_AT_WFMODE] =
    {
        .p_cmd = (uint8_t *) "AT+WFMODE=0\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_32,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* Restart */
    [ DA16200_AT_CMD_INDEX_AT_RESTART] =
    {
        .p_cmd = (uint8_t *) "AT+RESTART\r\n",
        .p_success_resp = (uint8_t *) "DONE",
        .max_resp_length = DA16200_STR_LEN_128,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_1000MS
    },
    /* 连接wifi热点 */
    [DA16200_AT_CMD_INDEX_AT_WFJAP] =
    {
        .p_cmd = (uint8_t *) "AT+WFJAP=101,4,1,15820474292\r\n",
        .p_success_resp = (uint8_t *) "+WFJAP:1",
        .max_resp_length = DA16200_STR_LEN_128,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_2000MS
    },
    /* TCP连接 */
    [DA16200_AT_CMD_INDEX_AT_TRTC] =
    {
        .p_cmd = (uint8_t *) "AT+TRTC=183.230.40.33,80\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_128,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_1000MS
    },
    /* 发送数据 */
    [DA16200_AT_CMD_INDEX_AT_SEND_DATA] =
    {
        .p_cmd = (uint8_t *) "AT+WFMODE=0\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_512,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_1000MS
    },


    /* Set Country Code */
    [DA16200_AT_CMD_INDEX_AT_WFCC] =
    {
        .p_cmd = (uint8_t *) "AT+WFCC=CH\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_32,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* Set Country Code */
    [ DA16200_AT_CMD_INDEX_AT_WFSAP] =
    {
        .p_cmd = (uint8_t *) "AT+WFSAP=Renesas_Wifi,3,1,12345678,1,CH\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_128,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* Set IP */
    [ DA16200_AT_CMD_INDEX_AT_NWIP] =
    {
        .p_cmd = (uint8_t *) "AT+NWIP=1,192.168.10.2,255.255.255.0,192.168.10.1\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_64,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_1000MS
    },
    /* Start DHCP */
    [ DA16200_AT_CMD_INDEX_AT_NWDHS] =
    {
        .p_cmd = (uint8_t *) "AT+NWDHS=1\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_64,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* DHCP IP area set*/
    [ DA16200_AT_CMD_INDEX_AT_NWDHR] =
    {
        .p_cmd = (uint8_t *) "AT+NWDHR=192.168.10.3,192.168.10.10\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_64,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* TCP server port set*/
    [ DA16200_AT_CMD_INDEX_AT_TRTS] =
    {
        .p_cmd = (uint8_t *) "AT+TRTS=80\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_64,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    },
    /* TCP server port message save */
    [ DA16200_AT_CMD_INDEX_AT_TRSAVE] =
    {
        .p_cmd = (uint8_t *) "AT+TRSAVE\r\n",
        .p_success_resp = (uint8_t *) "OK",
        .max_resp_length = DA16200_STR_LEN_32,
        .retry = DA16200_RETRY_VALUE_5,
        .retry_delay = DA16200_DELAY_200MS
    }


};

uint8_t g_wifi_receive_complete = 0;
uint8_t g_wifi_transmit_complete = 0;
uint16_t g_wifi_receive_time_count = 0;
static uint16_t count = 0;

static fsp_err_t wifi_command_ATZ(void);
static fsp_err_t wifi_command_ATE(void);
//static fsp_err_t wifi_command_ATF(void);
static fsp_err_t wifi_command_AT_TMRFNOINIT(void);
static fsp_err_t wifi_command_AT_WFMODE(void);
static fsp_err_t wifi_command_AT_RESTART(void);
static fsp_err_t wifi_command_AT_WFJAP(void);
static fsp_err_t wifi_command_AT_TRTC(void);

//static fsp_err_t wifi_command_AT_WFCC(void);
//static fsp_err_t wifi_command_AT_WFSAP(void);
//static fsp_err_t wifi_command_AT_NWIP(void);
//static fsp_err_t wifi_command_AT_NWDHS(void);
//static fsp_err_t wifi_command_AT_NWDHR(void);
//static fsp_err_t wifi_command_AT_TRTS(void);
//static fsp_err_t wifi_command_AT_TRSAVE(void);

/************************************************************************************
* Name:       wifi_command_ATZ
* Function:   Operate ATZ command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_ATZ(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_64] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    /**
     * ATZ
     * initialize AT command
     */

    do
    {
        /** AT MODODR command **/
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_ATZ].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_ATZ].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_ATZ].max_resp_length;

        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 500);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_ATZ].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }
        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_ATZ].retry_delay, BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_ATZ].retry);

    return retval;
}

/************************************************************************************
* Name:       wifi_command_ATE
* Function:   Operate ATE command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_ATE(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_32] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    /**
     * ATE
     * Echo AT command
     */

    do
    {
        /** AT MODODR command **/
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_ATE].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_ATE].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_ATE].max_resp_length;

        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 1000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_ATE].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_ATE].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_ATE].retry);

    return retval;
}


/************************************************************************************
* Name:       wifi_command_ATF
* Function:   Operate ATF command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_ATF(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_128] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_ATF].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_ATF].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_ATF].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 2000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_ATF].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_ATF].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_ATF].retry);

    return retval;
}
*/

/************************************************************************************
* Name:       wifi_command_AT_TMRFNOINIT
* Function:   Operate AT+TMRFNOINIT command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_AT_TMRFNOINIT(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_32] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    /**
     * AT+TMRFNOINIT = 0
     * AT Wifi normal boot
     */

    do
    {
        /** AT TMRFNOINIT command **/
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_TMRFNOINIT].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TMRFNOINIT].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_TMRFNOINIT].max_resp_length;

        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 1000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TMRFNOINIT].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_TMRFNOINIT].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_TMRFNOINIT].retry);

    return retval;
}


/************************************************************************************
* Name:       wifi_command_AT_WFMODE
* Function:   Operate AT+WFMODE command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_AT_WFMODE(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_32] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    /**
     * AT+WFMODE = 0
     * AT Wifi mode
     */

    do
    {
        /** AT MODODR command **/
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFMODE].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFMODE].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFMODE].max_resp_length;

        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 1000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFMODE].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFMODE].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFMODE].retry);

    return retval;
}


/************************************************************************************
* Name:       wifi_command_AT_RESTART
* Function:   Operate AT+RESTART command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_AT_RESTART(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_128] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    /**
     * AT+RESTART = 1
     * AT Country Code
     */

    do
    {
        /** AT MODODR command **/
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_RESTART].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_RESTART].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_RESTART].max_resp_length;

        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 5000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_RESTART].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_RESTART].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_RESTART].retry);

    return retval;
}

/************************************************************************************
* Name:       wifi_command_AT_WFJAP
* Function:   Operate AT+WFJAP command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_AT_WFJAP(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_128] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    /**
     * AT+WFJAP=101,4,1,15820474292\r\n
     * AT Country Code
     */

    do
    {
        /** AT MODODR command **/
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFJAP].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFJAP].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFJAP].max_resp_length;

        /** Clear respond memory **/
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 5000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFJAP].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFJAP].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFJAP].retry);

    return retval;
}


/************************************************************************************
* Name:       wifi_command_AT_TRTC
* Function:   Operate AT+TRTC command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
static fsp_err_t wifi_command_AT_TRTC(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_128] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    //AT+TRTC=183.230.40.33,80\r\n


    do
    {
        //AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTC].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTC].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTC].max_resp_length;

        //Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 5000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTC].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTC].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTC].retry);

    return retval;
}









/************************************************************************************
* Name:       wifi_command_AT_WFCC
* Function:   Operate AT+WFCC command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_WFCC(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_32] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    //AT+WFCC = CH


    do
    {
        //AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFCC].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFCC].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFCC].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 500);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFCC].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFCC].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFCC].retry);

    return retval;
}
*/


/************************************************************************************
* Name:       wifi_command_AT_WFSAP
* Function:   Operate AT+WFSAP command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_WFSAP(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_128] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    // AT+WFAP = 1


    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFSAP].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFSAP].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFSAP].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 3000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFSAP].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFSAP].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_WFSAP].retry);

    return retval;
}
*/

/************************************************************************************
* Name:       wifi_command_AT_NWIP
* Function:   Operate AT+NWIP command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_NWIP(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_64] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    // AT+WFIP = xxxxx


    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWIP].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWIP].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWIP].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 3000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWIP].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWIP].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWIP].retry);

    return retval;
}
*/

/************************************************************************************
* Name:       wifi_command_AT_NWDHS
* Function:   Operate AT+NWDHS command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_NWDHS(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_64] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    // AT+WWDHS = 1


    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHS].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHS].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHS].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 500);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHS].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHS].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHS].retry);

    return retval;
}
*/

/************************************************************************************
* Name:       wifi_command_AT_NWDHR
* Function:   Operate AT+NWDHR command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_NWDHR(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_64] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    // AT+NWDHR = xxxx


    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHR].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHR].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHR].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 2000);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHR].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHR].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_NWDHR].retry);

    return retval;
}
*/


/************************************************************************************
* Name:       wifi_command_AT_TRTS
* Function:   Operate AT+TRTS command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_TRTS(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_64] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    // AT+TRTS = xxxx    TCP port setting


    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTS].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTS].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTS].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 500);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTS].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTS].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRTS].retry);

    return retval;
}
*/

/************************************************************************************
* Name:       wifi_command_AT_TRSAVE
* Function:   Operate AT+TRSAVE command
* Parameters: none
* Return:     AT command operation status
************************************************************************************/
/*
static fsp_err_t wifi_command_AT_TRSAVE(void)
{
    uint16_t bytes_read = 0U;
    uint16_t bytes_write;
    uint8_t resp_buff[DA16200_STR_LEN_32] =
    {0};
    fsp_err_t retval = FSP_ERR_ASSERTION;
    fsp_err_t result;
    uint8_t retry_count = 0U;
    da16200_at_cmd_set_t  * p_cmd_set = g_da16200_cmd_set;
    uint8_t expected_resp_present;

    // AT+WFCC = CH


    do
    {
        // AT MODODR command
        bytes_write = (uint16_t) strlen((char *) p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRSAVE].p_cmd);
        wifi_serial_write((uint8_t*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRSAVE].p_cmd,bytes_write);

        bytes_read = p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRSAVE].max_resp_length;

        // Clear respond memory
        memset (resp_buff, 0 , sizeof(resp_buff));

        result = wifi_serial_read(resp_buff, &bytes_read, 300);

        if((FSP_SUCCESS == result) || (FSP_ERR_TIMEOUT == result))
        {
            expected_resp_present = is_str_present((const char *)resp_buff, (const char*)p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRSAVE].p_success_resp);
            if(SF_WIFI_TRUE == expected_resp_present)
            {
                retval = FSP_SUCCESS;
                break;
            }
        }

        R_BSP_SoftwareDelay(p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRSAVE].retry_delay,BSP_DELAY_UNITS_MILLISECONDS);

        ++retry_count;

    }
    while(retry_count < p_cmd_set[DA16200_AT_CMD_INDEX_AT_TRSAVE].retry);

    return retval;
}
*/


/************************************************************************************
* Name:       wifi_init
* Function:   initialization Wifi
* Parameters: none
* Return:     wifi initialization status
************************************************************************************/
fsp_err_t wifi_init(void)
{
    fsp_err_t status = FSP_SUCCESS;

    g_timer.p_api->open(g_timer.p_ctrl, g_timer.p_cfg);
    g_timer.p_api->start(g_timer.p_ctrl);
    g_wifi_uart.p_api->open(g_wifi_uart.p_ctrl, g_wifi_uart.p_cfg);

    status = wifi_command_ATZ();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    R_BSP_SoftwareDelay(100,BSP_DELAY_UNITS_MILLISECONDS);
    status = wifi_command_ATE();
    if(status != FSP_SUCCESS)
    {
        return status;
    }

    return status;

}

/************************************************************************************
* Name:       wifi_set
* Function:   setting Wifi
* Parameters: none
* Return:     wifi setting status
************************************************************************************/
fsp_err_t wifi_set(void)
{
    fsp_err_t status = FSP_SUCCESS;

//    status = wifi_command_ATF();
//    if(status != FSP_SUCCESS)
//    {
//        return status;
//    }

    status = wifi_command_AT_TMRFNOINIT();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    R_BSP_SoftwareDelay(100,BSP_DELAY_UNITS_MILLISECONDS);
    status = wifi_command_AT_WFMODE();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    R_BSP_SoftwareDelay(100,BSP_DELAY_UNITS_MILLISECONDS);
    status = wifi_command_AT_RESTART();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    R_BSP_SoftwareDelay(100,BSP_DELAY_UNITS_MILLISECONDS);
    status = wifi_command_AT_WFJAP();
    if(status != FSP_SUCCESS)
    {
       return status;
    }
    R_BSP_SoftwareDelay(100,BSP_DELAY_UNITS_MILLISECONDS);
    status = wifi_command_AT_TRTC();
    if(status != FSP_SUCCESS)
    {
       return status;
    }


/*
    status = wifi_command_AT_WFCC();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    status = wifi_command_AT_WFSAP();
    if(status != FSP_SUCCESS)
    {
        return status;
    }

    status = wifi_command_ATE();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    status = wifi_command_AT_NWIP();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    status = wifi_command_AT_NWDHS();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    status = wifi_command_AT_NWDHR();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    status = wifi_command_AT_TRTS();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
    status = wifi_command_AT_TRSAVE();
    if(status != FSP_SUCCESS)
    {
        return status;
    }
*/
    return status;
}

/************************************************************************************
* Name:       is_str_present
* Function:   compare string
* Parameters: p_resp_str, p_search_str
* Return:     comparing result
************************************************************************************/
uint8_t is_str_present(const char * p_resp_str, const char * p_search_str)
{
    if (strstr (p_resp_str, p_search_str))
    {
        return SF_WIFI_TRUE;
    }

    return SF_WIFI_FALSE;
}

/************************************************************************************
* Name:       wifi_serial_read
* Function:   read receive data
* Parameters: p_dest, p_bytes, timeout_ms
* Return:     read result
************************************************************************************/
fsp_err_t wifi_serial_read(uint8_t * p_dest, uint16_t * p_bytes, uint32_t timeout_ms)
{
    fsp_err_t status = FSP_SUCCESS;
    uint16_t bytes = *p_bytes;
    uint8_t temp_mem;
    uint16_t temp_count = 0;
    g_wifi_receive_time_count = 0;
    do
    {
          g_wifi_uart.p_api->read(g_wifi_uart.p_ctrl, &temp_mem, 1);
          if(g_wifi_receive_complete)
          {
              g_wifi_receive_complete = 0;
              *p_dest = temp_mem;
               p_dest++;
               temp_count++;
               if(temp_count >= bytes)
               {
                   break;
               }
          }
          if(g_wifi_receive_time_count >= timeout_ms)
          {
               status = FSP_ERR_TIMEOUT;
               break;
          }

    }while(1);

    return status;
}


/************************************************************************************
* Name:       wifi_serial_write
* Function:   write data
* Parameters: p_src, bytes
* Return:     write result
************************************************************************************/
fsp_err_t wifi_serial_write(uint8_t * p_src, uint16_t bytes)
{
    fsp_err_t status = FSP_SUCCESS;


    status = g_wifi_uart.p_api->write(g_wifi_uart.p_ctrl, p_src, bytes);

    return(status);

}

/************************************************************************************
* Name:       user_uart_callback
* Function:   write data callback
* Parameters: p_args
* Return:     write result
************************************************************************************/
void user_uart_callback(uart_callback_args_t *p_args)
{
    if(UART_EVENT_RX_COMPLETE == p_args->event)
    {
        g_wifi_receive_complete = 1;
    }


}

/************************************************************************************
* Name:       timer_1ms_callback
* Function:   1ms callback
* Parameters: p_args
* Return:     1ms timing
************************************************************************************/
void timer_1ms_callback(timer_callback_args_t *p_args)
{

    FSP_PARAMETER_NOT_USED(p_args);
    g_wifi_receive_time_count++;
    if(g_demo_data.led_blink_flag)
    {
        g_demo_data.led_blink_count++;
        if(g_demo_data.led_blink_count >= 1000)
        {
            count++;
            g_demo_data.led_blink_count = 0;
            if(count%2)
            {
                g_ioport.p_api->pinWrite(g_ioport.p_ctrl,LED3, BSP_IO_LEVEL_HIGH);
            }
            else
            {
                g_ioport.p_api->pinWrite(g_ioport.p_ctrl,LED3, BSP_IO_LEVEL_LOW);
            }

        }
    }
}



