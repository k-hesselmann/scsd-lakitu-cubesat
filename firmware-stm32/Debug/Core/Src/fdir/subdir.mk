################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/fdir/fdir.c 

OBJS += \
./Core/Src/fdir/fdir.o 

C_DEPS += \
./Core/Src/fdir/fdir.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/fdir/%.o Core/Src/fdir/%.su Core/Src/fdir/%.cyclo: ../Core/Src/fdir/%.c Core/Src/fdir/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../FATFS/App -I../FATFS/Target -I../Middlewares/Third_Party/FatFs/src -I../Core/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Device/ST/STM32L4xx/Include -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-fdir

clean-Core-2f-Src-2f-fdir:
	-$(RM) ./Core/Src/fdir/fdir.cyclo ./Core/Src/fdir/fdir.d ./Core/Src/fdir/fdir.o ./Core/Src/fdir/fdir.su

.PHONY: clean-Core-2f-Src-2f-fdir

