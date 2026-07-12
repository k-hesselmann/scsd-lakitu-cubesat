################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/fsw/fsm.c 

OBJS += \
./Core/Src/fsw/fsm.o 

C_DEPS += \
./Core/Src/fsw/fsm.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/fsw/%.o Core/Src/fsw/%.su Core/Src/fsw/%.cyclo: ../Core/Src/fsw/%.c Core/Src/fsw/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Core/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Device/ST/STM32L4xx/Include -IC:/Users/Frederik/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-fsw

clean-Core-2f-Src-2f-fsw:
	-$(RM) ./Core/Src/fsw/fsm.cyclo ./Core/Src/fsw/fsm.d ./Core/Src/fsw/fsm.o ./Core/Src/fsw/fsm.su

.PHONY: clean-Core-2f-Src-2f-fsw

