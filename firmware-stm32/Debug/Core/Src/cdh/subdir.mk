################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/cdh/baro_diag.c \
../Core/Src/cdh/cdh.c \
../Core/Src/cdh/cdh_debug.c \
../Core/Src/cdh/cdh_fdir.c \
../Core/Src/cdh/gps_diag.c \
../Core/Src/cdh/gps_equipment_handler.c \
../Core/Src/cdh/m10s.c \
../Core/Src/cdh/mpu6050.c \
../Core/Src/cdh/mpu6050_equipment_handler.c \
../Core/Src/cdh/ms5607.c \
../Core/Src/cdh/ms5607_equipment_handler.c 

OBJS += \
./Core/Src/cdh/baro_diag.o \
./Core/Src/cdh/cdh.o \
./Core/Src/cdh/cdh_debug.o \
./Core/Src/cdh/cdh_fdir.o \
./Core/Src/cdh/gps_diag.o \
./Core/Src/cdh/gps_equipment_handler.o \
./Core/Src/cdh/m10s.o \
./Core/Src/cdh/mpu6050.o \
./Core/Src/cdh/mpu6050_equipment_handler.o \
./Core/Src/cdh/ms5607.o \
./Core/Src/cdh/ms5607_equipment_handler.o 

C_DEPS += \
./Core/Src/cdh/baro_diag.d \
./Core/Src/cdh/cdh.d \
./Core/Src/cdh/cdh_debug.d \
./Core/Src/cdh/cdh_fdir.d \
./Core/Src/cdh/gps_diag.d \
./Core/Src/cdh/gps_equipment_handler.d \
./Core/Src/cdh/m10s.d \
./Core/Src/cdh/mpu6050.d \
./Core/Src/cdh/mpu6050_equipment_handler.d \
./Core/Src/cdh/ms5607.d \
./Core/Src/cdh/ms5607_equipment_handler.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/cdh/%.o Core/Src/cdh/%.su Core/Src/cdh/%.cyclo: ../Core/Src/cdh/%.c Core/Src/cdh/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../USB_DEVICE/App -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/Third_Party/FatFs/src -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/Third_Party/FatFs/src/option -I../FATFS/App -I../FATFS/Target -I../Middlewares/Third_Party/FatFs/src -I../USB_DEVICE/Target -I../Core/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Device/ST/STM32L4xx/Include -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Include -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Device/ST/STM32L4xx/Include -IC:/Users/alber/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-cdh

clean-Core-2f-Src-2f-cdh:
	-$(RM) ./Core/Src/cdh/baro_diag.cyclo ./Core/Src/cdh/baro_diag.d ./Core/Src/cdh/baro_diag.o ./Core/Src/cdh/baro_diag.su ./Core/Src/cdh/cdh.cyclo ./Core/Src/cdh/cdh.d ./Core/Src/cdh/cdh.o ./Core/Src/cdh/cdh.su ./Core/Src/cdh/cdh_debug.cyclo ./Core/Src/cdh/cdh_debug.d ./Core/Src/cdh/cdh_debug.o ./Core/Src/cdh/cdh_debug.su ./Core/Src/cdh/cdh_fdir.cyclo ./Core/Src/cdh/cdh_fdir.d ./Core/Src/cdh/cdh_fdir.o ./Core/Src/cdh/cdh_fdir.su ./Core/Src/cdh/gps_diag.cyclo ./Core/Src/cdh/gps_diag.d ./Core/Src/cdh/gps_diag.o ./Core/Src/cdh/gps_diag.su ./Core/Src/cdh/gps_equipment_handler.cyclo ./Core/Src/cdh/gps_equipment_handler.d ./Core/Src/cdh/gps_equipment_handler.o ./Core/Src/cdh/gps_equipment_handler.su ./Core/Src/cdh/m10s.cyclo ./Core/Src/cdh/m10s.d ./Core/Src/cdh/m10s.o ./Core/Src/cdh/m10s.su ./Core/Src/cdh/mpu6050.cyclo ./Core/Src/cdh/mpu6050.d ./Core/Src/cdh/mpu6050.o ./Core/Src/cdh/mpu6050.su ./Core/Src/cdh/mpu6050_equipment_handler.cyclo ./Core/Src/cdh/mpu6050_equipment_handler.d ./Core/Src/cdh/mpu6050_equipment_handler.o ./Core/Src/cdh/mpu6050_equipment_handler.su ./Core/Src/cdh/ms5607.cyclo ./Core/Src/cdh/ms5607.d ./Core/Src/cdh/ms5607.o ./Core/Src/cdh/ms5607.su ./Core/Src/cdh/ms5607_equipment_handler.cyclo ./Core/Src/cdh/ms5607_equipment_handler.d ./Core/Src/cdh/ms5607_equipment_handler.o ./Core/Src/cdh/ms5607_equipment_handler.su

.PHONY: clean-Core-2f-Src-2f-cdh

