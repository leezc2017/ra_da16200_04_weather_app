################################################################################
# FSP CTSU driver build rules.
################################################################################
C_SRCS += ../ra/fsp/src/r_ctsu/r_ctsu.c
C_DEPS += ./ra/fsp/src/r_ctsu/r_ctsu.d
OBJS += ./ra/fsp/src/r_ctsu/r_ctsu.o
SREC += 4.weather_app.srec
MAP += 4.weather_app.map

ra/fsp/src/r_ctsu/%.o: ../ra/fsp/src/r_ctsu/%.c
	$(file > $@.in,-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -O2 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-strict-aliasing -Wunused -Wuninitialized -Wall -Wextra -Wmissing-declarations -Wconversion -Wpointer-arith -Wshadow -Wlogical-op -Waggregate-return -Wfloat-equal -g -D_RENESAS_RA_ -D_RA_CORE=CM33 -DQE_TOUCH_CONFIGURATION -D_RA_ORDINAL=1 -I"../ra_cfg/fsp_cfg/bsp" -I"." -I"../ra_gen" -I"../ra_cfg/fsp_cfg" -I"../src" -I"../ra/fsp/inc" -I"../ra/fsp/inc/api" -I"../ra/fsp/inc/instances" -I"../qe_gen" -I"../ra/arm/CMSIS_6/CMSIS/Core/Include" -std=c99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" -x c "$<")
	@echo Building file: $< && arm-none-eabi-gcc @"$@.in"
