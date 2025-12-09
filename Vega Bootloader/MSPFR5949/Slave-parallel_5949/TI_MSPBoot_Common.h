/*
 * \file   TI_MSPBoot_Common.h
 *
 * \brief  Header with Common definitions used by the project
 *
 */


#ifndef __TI_MSPBoot_COMMON_H__
#define __TI_MSPBoot_COMMON_H__

//
// Include files
//
#include <stdint.h>
#include "TI_MSPBoot_Config.h"

//
// Type definitions
//
/*! Boolean type definition */
typedef enum
{
    FALSE_t=0,
    TRUE_t
} tBOOL;

//
// MACROS
//
/*! Used for debugging purposes. This macro valuates an expression when NDEBUG
    is not defined and if true, it stays in an infinite loop */
#ifndef ASSERT_H
    #ifndef NDEBUG
        #define ASSERT_H(expr) \
                if (!expr) {\
                 while (1);\
                }
    #else
        #define ASSERT_H(expr)
    #endif
#endif

#define NULL    0x00            /*! NULL definition */

                
//
// Function return values
//
#define RET_OK              0   /*! Function returned OK */
#define RET_PARAM_ERROR     1   /*! Parameters are incorrect */
#define RET_JUMP_TO_APP     2   /*! Function exits and jump to application */


#endif //__TI_MSPBoot_COMMON_H__
