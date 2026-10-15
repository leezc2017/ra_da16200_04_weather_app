################################################################################
# QE Touch generated source build rules.
################################################################################
C_SRCS += ../qe_gen/qe_touch_config.c
C_DEPS += ./qe_gen/qe_touch_config.d
OBJS += ./qe_gen/qe_touch_config.o
SREC += 4.weather_app.srec
MAP += 4.weather_app.map

qe_gen/%.o: ../qe_gen/%.c
	$(file > $@.in,-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -O2 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-strict-aliasing -Wunused -Wuninitialized -Wall -Wextra -Wmissing-declarations -Wconversion -Wpointer-arith -Wshadow -Wlogical-op -Waggregate-return -Wfloat-equal -g -D_RENESAS_RA_ -D_RA_CORE=CM33 -DQE_TOUCH_CONFIGURATION -D_RA_ORDINAL=1 -I"../ra_cfg/fsp_cfg/bsp" -I"." -I"../ra_gen" -I"../ra_cfg/fsp_cfg" -I"../src" -I"../ra/fsp/inc" -I"../ra/fsp/inc/api" -I"../ra/fsp/inc/instances" -I"../qe_gen" -I"../ra/arm/CMSIS_6/CMSIS/Core/Include" -std=c99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" -x c "$<")
	@echo Building file: $< && arm-none-eabi-gcc @"$@.in"
