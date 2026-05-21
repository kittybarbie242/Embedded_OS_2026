obj-m += relay_driver.o led_driver.o button_driver.o pzem_driver.o st7735_driver.o

app:
	$(CC) $(CFLAGS) $(LDFLAGS) main.c tft_gui.c -o app_main -lpthread -lmosquitto

clean:
	rm -f app_main *.o *.ko *.mod.* modules.order Module.symvers
